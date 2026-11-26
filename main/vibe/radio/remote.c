#include "catalog_service.h"
#include "remote.h"
#include "stream.h"
#include "vibe.h"
#include "adpcm.h"
#include "bsp_audio.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_coexist.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "cJSON.h"
#include "media_workspace.h"
#include "ble.h"
static SemaphoreHandle_t mutex;
static vibe_radio_stream_t *stream;
static bool remote_failed;
static const char *TAG="vibe_radio";
static void radio_coex(esp_coex_prefer_t preference) {
    esp_err_t result=esp_coex_preference_set(preference);
    if (result!=ESP_OK) ESP_LOGW(TAG,"Radio coexistence hint unavailable: %s",esp_err_to_name(result));
}
bool vibe_radio_remote_init(void) { if (!mutex) mutex=xSemaphoreCreateMutex();return mutex!=NULL; }
void vibe_radio_remote_receive(const uint8_t *packet,size_t n,uint32_t generation,bool ble) {
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (stream) (void)vibe_radio_stream_receive(stream,packet,n,generation,ble);
    xSemaphoreGive(mutex);
}
void vibe_radio_remote_status(const char *session,bool failed) {
    if (!mutex || !session || strlen(session)!=16) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    char id[17];snprintf(id,sizeof(id),"%016" PRIx64,stream?stream->session:0);
    if (stream && !strcmp(id,session)) remote_failed=failed;
    xSemaphoreGive(mutex);
}
typedef struct { uint64_t session;uint32_t next,last_wifi,last_ble;uint16_t mask; } progress_t;
static void progress(const progress_t *s) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);int rssi=v_status.wifi?v_status.rssi:-127;xSemaphoreGive(v_mutex);
    char line[224];snprintf(line,sizeof(line),"{\"event\":\"radio.progress\",\"session\":\"%016" PRIx64 "\",\"next\":%" PRIu32 ",\"mask\":%u,\"wifi\":%" PRIu32 ",\"ble\":%" PRIu32 ",\"rssi\":%d}",s->session,s->next,s->mask,s->last_wifi,s->last_ble,rssi);
    vibe_send_json_both(line);
}
vibe_radio_result_t vibe_radio_session(const char *url,bool (*cancel)(void),int16_t *pcm) {
    vibe_media_lease_t lease;
    size_t required=sizeof(vibe_radio_stream_t)+V_RADIO_WINDOW*V_RADIO_BYTES;
    if (!pcm || !vibe_media_acquire(V_MEDIA_RADIO,required,&lease)) {
        vibe_radio_status(V_RADIO_ERROR,"音频内存不足");return V_RADIO_SESSION_NO_MEMORY;
    }
    vibe_radio_stream_t *s=lease.memory;
    uint8_t *buffers[V_RADIO_WINDOW];
    for (unsigned i=0;i<V_RADIO_WINDOW;i++) buffers[i]=(uint8_t *)(s+1)+i*V_RADIO_BYTES;
    uint64_t nonce;esp_fill_random(&nonce,sizeof(nonce));if (!nonce) nonce=1;
    const uint32_t owner=vibe_network_generation();vibe_radio_stream_init(s,nonce,owner,buffers);
    cJSON *open=cJSON_CreateObject();char id[17];snprintf(id,sizeof(id),"%016" PRIx64,nonce);
    bool valid=open && cJSON_AddStringToObject(open,"event","radio.open") && cJSON_AddStringToObject(open,"session",id) && cJSON_AddStringToObject(open,"url",url) && cJSON_AddNumberToObject(open,"window",V_RADIO_WINDOW) && cJSON_AddNumberToObject(open,"format",V_RADIO_FORMAT);
    char *request=valid?cJSON_PrintUnformatted(open):NULL;cJSON_Delete(open);
    if (!request) { vibe_media_release(lease);vibe_radio_status(V_RADIO_ERROR,"音频内存不足");return V_RADIO_SESSION_NO_MEMORY; }
    xSemaphoreTake(mutex,portMAX_DELAY);stream=s;remote_failed=false;xSemaphoreGive(mutex);
    vibe_ble_radio_ingress(true);
    /* Keep BLE connection events and its audio credits timely while the shared
     * radio scans/associates on Wi-Fi. UDP audio uses little Wi-Fi airtime.
     * This session alone owns the hint; voice and idle return to balance. */
    radio_coex(ESP_COEX_PREFER_BT);
    bool played=false,failed=false,started=false,rendering=false;unsigned frames=0,underruns=0;
    uint64_t begin=vibe_now(),last_audio=begin,last_open=0,last_ack=0,last_report=0,empty_since=0;
    if (bsp_audio_set_format(V_RADIO_RATE,16,1)!=ESP_OK) failed=true;
    ESP_LOGI(TAG,"Computer playback start: buffer=%u PCM=%u owner=%lu",(unsigned)(sizeof(*s)+V_RADIO_WINDOW*V_RADIO_BYTES),V_RADIO_SAMPLES*2,(unsigned long)owner);
    while (!failed && !cancel() && vibe_network_generation_matches(owner)) {
        uint64_t now=vibe_now();
        if (!frames && (!last_open || now-last_open>=1000)) { vibe_send_json_both(request);last_open=now; }
        uint8_t frame[V_RADIO_BYTES];bool have=false;progress_t ack={0};
        xSemaphoreTake(mutex,portMAX_DELAY);
        failed=remote_failed;
        if (!started && (s->mask&((1u<<V_RADIO_PREFILL)-1))==((1u<<V_RADIO_PREFILL)-1)) started=true;
        if (started) have=vibe_radio_stream_pop(s,frame);
        /* Copy metadata only: the ring remains protected and never lives on this stack. */
        ack.session=s->session;ack.next=s->next;ack.mask=s->mask;ack.last_wifi=s->last_wifi;ack.last_ble=s->last_ble;
        xSemaphoreGive(mutex);
        if (now-last_report>=5000) {
            last_report=now;
            ESP_LOGI(TAG,"Playback progress: frames=%u next=%lu mask=%u wifi=%lu ble=%lu",frames,(unsigned long)ack.next,ack.mask,(unsigned long)ack.last_wifi,(unsigned long)ack.last_ble);
        }
        if (failed || cancel() || !vibe_network_generation_matches(owner)) break;
        if (have || now-last_ack>=200) { progress(&ack);last_ack=now; }
        if (have) {
            empty_since=0;
            if (adpcm_decode_block(frame,sizeof(frame),V_RADIO_SAMPLES,pcm,V_RADIO_SAMPLES)!=V_RADIO_SAMPLES) { failed=true;break; }
            xSemaphoreTake(v_mutex,portMAX_DELAY);unsigned volume=v_prefs.volume;xSemaphoreGive(v_mutex);bsp_audio_set_volume(volume);
            if (bsp_audio_write(pcm,V_RADIO_SAMPLES*2)!=ESP_OK) { failed=true;break; }
            if (!rendering) { vibe_radio_status(V_RADIO_PLAYING,"");rendering=true; }
            if (!played) { vibe_radio_played(url);played=true; }
            ++frames;last_audio=vibe_now();
        } else {
            /* I2S retains up to 60 ms of queued PCM. A short ingress jitter
             * must not discard the running state and demand another
             * prefill. Bound the grace below that DMA reserve; longer starvation
             * still enters buffering rather than busy-waiting or replaying. */
            if (started) {
                if (!empty_since) empty_since=now;
                if (now-empty_since>=40) { started=false;rendering=false;empty_since=0;++underruns;vibe_radio_status(V_RADIO_BUFFERING,""); }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        now=vibe_now();
        if ((!played && now-begin>20000) || (played && now-last_audio>8000)) failed=true;
    }
    xSemaphoreTake(mutex,portMAX_DELAY);stream=NULL;xSemaphoreGive(mutex);
    char close[96];snprintf(close,sizeof(close),"{\"event\":\"radio.close\",\"session\":\"%s\"}",id);vibe_send_json_both(close);
    vibe_ble_radio_ingress(false);
    vibe_media_release(lease);free(request);
    radio_coex(ESP_COEX_PREFER_BALANCE);
    ESP_LOGI(TAG,"Computer playback stop: frames=%u underruns=%u failed=%d",frames,underruns,failed);
    if (failed) { vibe_radio_status(V_RADIO_ERROR,"电台连接失败");return V_RADIO_SESSION_REMOTE_ERROR; }
    return played?V_RADIO_SESSION_PLAYED:V_RADIO_SESSION_FAILED;
}
