#include "vibe.h"
#include "radio/catalog_service.h"
#include "radio/remote.h"
#include "adpcm.h"
#include "voice_capture.h"
#include "ble.h"
#include "voice_wire.h"
#include "media_workspace.h"
#include "ui_sfx.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static TaskHandle_t worker;
static int16_t *capture_pcm;
static const char *TAG="vibe_audio";
static portMUX_TYPE request_mux=portMUX_INITIALIZER_UNLOCKED;
static unsigned requests;
static unsigned take_requests(void) {
    portENTER_CRITICAL(&request_mux); unsigned r=requests; requests=0; portEXIT_CRITICAL(&request_mux); return r;
}
static bool pending(void) {
    portENTER_CRITICAL(&request_mux); bool yes=requests!=0; portEXIT_CRITICAL(&request_mux); return yes;
}
bool vibe_audio_request(unsigned action) {
    if (!worker) return false;
    portENTER_CRITICAL(&request_mux);
    requests|=action;
    portEXIT_CRITICAL(&request_mux);
    xTaskNotifyGive(worker); return true;
}
static bool radio_cancel(void) {
    if (pending()) return true;
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    bool cancel=!v_model.radio_active || !v_model.radio_requested || !v_status.peer || !v_status.radio_pcm || v_status.provisioning;
    xSemaphoreGive(v_mutex);return cancel;
}
static void set_busy(bool yes) {
    xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.audio_busy=yes; xSemaphoreGive(v_mutex);
}
static bool prepare_audio(const char *operation) {
    esp_err_t result=bsp_audio_init();
    if (result==ESP_OK) result=bsp_audio_wake();
    xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.audio_fault=result!=ESP_OK; xSemaphoreGive(v_mutex);
    if (result!=ESP_OK) ESP_LOGE(TAG,"%s preparation failed: %s",operation,esp_err_to_name(result));
    return result==ESP_OK;
}
static void alarm_play(int16_t *pcm) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);unsigned volume=v_prefs.volume;xSemaphoreGive(v_mutex);
    if (!prepare_audio("alarm")) return;
    if (bsp_audio_set_format(UI_SFX_RATE,16,1)!=ESP_OK) return;
    bsp_audio_set_volume(volume);
    unsigned total=ui_sfx_samples(UI_SFX_ALARM);
    for (unsigned pos=0;pos<total;) {
        if (pending()) {xSemaphoreTake(v_mutex,portMAX_DELAY);bool recording=v_model.recording;xSemaphoreGive(v_mutex);if (recording) break;}
        unsigned count=total-pos<256 ? total-pos : 256;
        for (unsigned i=0;i<count;i++) pcm[i]=ui_sfx_sample(UI_SFX_ALARM,pos+i);
        if (bsp_audio_write(pcm,count*2)!=ESP_OK) break;
        pos+=count;
    }
}
static void process_audio(unsigned actions) {
    /* Join the optional weather worker before allocating/using audio resources. */
    vibe_weather_pause();
    if (actions&(V_START_AUDIO|V_ALARM|V_AGENT_ALERT)) {
        if (actions&V_START_AUDIO) { set_busy(true); vibe_voice_capture(capture_pcm); }
        if (actions&(V_ALARM|V_AGENT_ALERT)) { set_busy(true); alarm_play(capture_pcm); }
    }
    unsigned failures=0;
    while (!radio_cancel()) {
        /* The network owner may still be completing the previous voice tail.
         * Do not allocate/reuse that history for radio, or wait on a new press. */
        if (vibe_media_owner()==V_MEDIA_VOICE) {ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20));continue;}
        set_busy(true);
        vibe_network_radio_activity(true);
        if (!prepare_audio("radio")) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);v_model.radio_requested=false;xSemaphoreGive(v_mutex);
            vibe_radio_status(V_RADIO_ERROR,"音频暂不可用");break;
        }
        char url[257];vibe_radio_url(url,sizeof(url));
        vibe_radio_status(V_RADIO_CONNECTING,"");
        uint64_t started=vibe_now();
        const vibe_radio_result_t result=vibe_radio_session(url,radio_cancel,capture_pcm);
        if (radio_cancel()) break;
        if (result==V_RADIO_SESSION_NO_MEMORY || result==V_RADIO_SESSION_REMOTE_ERROR) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);v_model.radio_requested=false;xSemaphoreGive(v_mutex);
            break; /* Keep the precise error; retrying cannot repair a RAM shortage. */
        }
        if (result==V_RADIO_SESSION_PLAYED && vibe_now()-started>30000) failures=0;
        if (++failures>=3) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);v_model.radio_requested=false;xSemaphoreGive(v_mutex);
            vibe_radio_status(V_RADIO_ERROR,"电台连接失败");break;
        }
        /* No decoder, TLS or powered codec during reconnect backoff. */
        bsp_audio_sleep();set_busy(false);vibe_radio_status(V_RADIO_CONNECTING,"");
        vibe_network_radio_activity(false);
        /* An established stream already spent time draining its FIFO and
         * timing out. Do not add two more silent seconds before its first
         * reconnect. Repeated short failures still back off and stop at three. */
        const unsigned wait_ms=failures==1?(result==V_RADIO_SESSION_PLAYED?0:2000):10000;
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(wait_ms));
    }
    esp_err_t result=bsp_audio_sleep();
    if (result!=ESP_OK) {
        ESP_LOGE(TAG,"Idle suspend failed: %s",esp_err_to_name(result));
        xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.audio_fault=true;xSemaphoreGive(v_mutex);
    }
    set_busy(false);
    vibe_network_radio_activity(false);
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    v_status.radio_playing=false;
    if (v_status.radio_state!=V_RADIO_ERROR) v_status.radio_state=V_RADIO_STOPPED;
    xSemaphoreGive(v_mutex);
    vibe_weather_resume();
}
static void audio_task(void *arg) {
    (void)arg;
    if (bsp_audio_init()!=ESP_OK) {
        xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.audio_fault=true; xSemaphoreGive(v_mutex);
    }
    bsp_audio_sleep();
    for (;;) {
        if (!pending()) ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        process_audio(take_requests());
    }
}
esp_err_t vibe_audio_init(void) {
    if (worker) return ESP_OK;
    if (!vibe_radio_remote_init()) return ESP_ERR_NO_MEM;
    /* Voice is core functionality: reserve its small PCM block at startup,
     * including while radio owns its much larger, disposable workspace. */
    capture_pcm=malloc(ADPCM_BLOCK_SAMPLES*2);
    if (!capture_pcm) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task,"vibe_audio",8192,NULL,6,&worker)==pdPASS) return ESP_OK;
    free(capture_pcm);capture_pcm=NULL;worker=NULL;
    return ESP_ERR_NO_MEM;
}
