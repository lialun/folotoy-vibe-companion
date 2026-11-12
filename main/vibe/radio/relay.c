#include "relay.h"
#include "cJSON.h"
#include "relay_buffer.h"
#include "catalog_service.h"
#include "transport.h"
#include "vibe.h"
#include "adpcm.h"
#include "diagnostics.h"
#include "bsp_audio.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SemaphoreHandle_t mutex;
static vibe_relay_buffer_t buffer;
static vibe_route_t source;
static bool aborted,failed;
static const char *TAG="vibe_relay";
esp_err_t vibe_relay_init(void) {
    mutex=xSemaphoreCreateMutex();return mutex?ESP_OK:ESP_ERR_NO_MEM;
}
bool vibe_relay_receive(vibe_route_t route,const void *bytes,size_t size) {
    if (!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ok=!aborted && vibe_route_equal(route,source) && vibe_relay_push(&buffer,bytes,size);
    xSemaphoreGive(mutex);return ok;
}
void vibe_relay_abort(void) {
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);aborted=true;xSemaphoreGive(mutex);
}
void vibe_relay_control(const cJSON *o) {
    const cJSON *stream=cJSON_GetObjectItemCaseSensitive(o,"stream");
    if (!mutex || !cJSON_IsNumber(stream)) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (buffer.stream && stream->valuedouble==buffer.stream) failed=true;
    xSemaphoreGive(mutex);
}
static void event(const char *kind,uint32_t stream,uint32_t received,uint32_t limit,const char *url) {
    cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"event",kind);cJSON_AddNumberToObject(o,"stream",stream);
    cJSON_AddNumberToObject(o,"next",received);cJSON_AddNumberToObject(o,"limit",limit);
    if (url) { cJSON_AddStringToObject(o,"url",url);cJSON_AddNumberToObject(o,"rate",V_RELAY_RATE);cJSON_AddNumberToObject(o,"samples",V_RELAY_SAMPLES); }
    char *s=cJSON_PrintUnformatted(o);if (s) { vibe_send_json(s);free(s); }cJSON_Delete(o);
}
static void checkpoint(const char *phase,int result) {
    vibe_radio_diagnostic_t d={phase,result,heap_caps_get_free_size(MALLOC_CAP_8BIT),heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)};
    xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.radio_diagnostic=d;xSemaphoreGive(v_mutex);
    ESP_LOGI(TAG,"%s result=%d free=%u largest=%u",phase,result,(unsigned)d.free,(unsigned)d.largest);
}
int vibe_relay_play(const char *url,bool (*cancel)(void),int16_t *pcm) {
    vibe_relay_buffer_t allocated={0};
    const size_t segment=V_RELAY_SEGMENT_FRAMES*V_RELAY_BYTES,reserve=16*1024;
    esp_err_t format=bsp_audio_set_format(V_RELAY_RATE,16,1);
    if (format!=ESP_OK) { vibe_diagnostics_record(V_DIAG_RADIO_OUTPUT,format);return V_RADIO_SESSION_FAILED; }
    /* Two required segments (~15.3 KiB); the third is optional (~23 KiB).
     * No reader task, separate PCM output or heavy decoder. */
    for (unsigned i=0;i<V_RELAY_SEGMENTS;i++) {
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT)<reserve+segment+128) break;
        allocated.segments[i]=malloc(segment);
        if (!allocated.segments[i]) break;
        allocated.capacity+=V_RELAY_SEGMENT_FRAMES;
    }
    /* Account for allocator overhead and concurrent small control allocations. */
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT)<reserve && allocated.segments[2]) {
        free(allocated.segments[2]);allocated.segments[2]=NULL;allocated.capacity-=V_RELAY_SEGMENT_FRAMES;
    }
    int result=V_RADIO_SESSION_FAILED;
    uint32_t stream=esp_random();if (!stream) stream=1;
    if (allocated.capacity<2*V_RELAY_SEGMENT_FRAMES || heap_caps_get_free_size(MALLOC_CAP_8BIT)<reserve) {
        vibe_diagnostics_record(V_DIAG_RADIO_MEMORY,(int32_t)(2*segment));
        vibe_radio_status(V_RADIO_ERROR,"播放内存不足");checkpoint("relay-memory",-2);result=V_RADIO_SESSION_NO_MEMORY;goto cleanup;
    }
    vibe_route_t route=vibe_transport_route();
    if (!route.kind) goto cleanup;
    xSemaphoreTake(mutex,portMAX_DELAY);buffer=allocated;buffer.stream=stream;source=route;aborted=failed=false;xSemaphoreGive(mutex);
    checkpoint("relay-start",allocated.capacity*V_RELAY_BYTES);
    bool playing=false,verified=false;
    unsigned failure=0;
    unsigned applied_volume=101;
    uint64_t started=vibe_now(),last_data=started,last_credit=0,last_open=0;
    uint32_t previous_received=0;
    uint8_t encoded[V_RELAY_BYTES];
    while (!cancel() && vibe_route_equal(route,vibe_transport_route())) {
        uint64_t now=vibe_now();
        xSemaphoreTake(mutex,portMAX_DELAY);
        bool stop=aborted,problem=failed;
        uint32_t received=buffer.received,limit=buffer.played+buffer.capacity;
        unsigned queued=buffer.received-buffer.played;
        if (!playing && queued>=V_RELAY_PREFILL) playing=true;
        bool have=playing && vibe_relay_pop(&buffer,encoded);
        xSemaphoreGive(mutex);
        if (stop || problem) { if (problem) failure=V_DIAG_RADIO_REMOTE;break; }
        if (received!=previous_received) { previous_received=received;last_data=now; }
        if (!received && (!last_open || now-last_open>=1000)) {
            event("relay.open",stream,received,limit,url);last_open=now;
        }
        if (!last_credit || now-last_credit>=200) {
            event("relay.credit",stream,received,limit,NULL);last_credit=now;
        }
        if (have) {
            if (adpcm_decode_block(encoded,sizeof(encoded),V_RELAY_SAMPLES,pcm,ADPCM_BLOCK_SAMPLES)!=V_RELAY_SAMPLES) { failure=V_DIAG_RADIO_DECODE;break; }
            vibe_prefs_t prefs;vibe_snapshot(NULL,&prefs,NULL);
            if (prefs.volume!=applied_volume) { applied_volume=prefs.volume;bsp_audio_set_volume(applied_volume); }
            if (bsp_audio_write(pcm,V_RELAY_SAMPLES*2)!=ESP_OK) { failure=V_DIAG_RADIO_OUTPUT;break; }
            result=V_RADIO_SESSION_PLAYED;
            if (!verified) { verified=true;vibe_radio_played(url);vibe_radio_status(V_RADIO_PLAYING,""); }
        } else {
            if (playing) { playing=false;vibe_radio_status(V_RADIO_BUFFERING,""); }
            /* Sleep between arrivals, no separate task or busy polling. */
            ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(25));
        }
        if (now-last_data>20000) { failure=V_DIAG_RADIO_TIMEOUT;break; }
        if (playing && have) vibe_radio_status(V_RADIO_PLAYING,"");
    }
    xSemaphoreTake(mutex,portMAX_DELAY);buffer=(vibe_relay_buffer_t){0};source=(vibe_route_t){0};aborted=true;xSemaphoreGive(mutex);
    event("relay.close",stream,0,0,NULL);
    if (failure && !cancel()) vibe_diagnostics_record(failure,(int32_t)stream);
    if (!cancel()) { vibe_radio_status(V_RADIO_ERROR,"电台连接失败");checkpoint("relay-ended",-1); }
cleanup:
    for (unsigned i=0;i<V_RELAY_SEGMENTS;i++) free(allocated.segments[i]);
    return result;
}
