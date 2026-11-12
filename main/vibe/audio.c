#include "vibe.h"
#include "transport.h"
#include "radio/catalog_service.h"
#include "radio/relay.h"
#include "diagnostics.h"
#include "adpcm.h"
#include "ui_sfx.h"
#include "bsp_audio.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static TaskHandle_t worker;
static int16_t *capture_pcm;
static const char *TAG="vibe_audio";
static portMUX_TYPE request_mux=portMUX_INITIALIZER_UNLOCKED;
static unsigned requests;
static uint64_t voice_requested_at;
static unsigned take_requests(void) {
    portENTER_CRITICAL(&request_mux); unsigned r=requests; requests=0; portEXIT_CRITICAL(&request_mux); return r;
}
static bool pending(void) {
    portENTER_CRITICAL(&request_mux); bool yes=requests!=0; portEXIT_CRITICAL(&request_mux); return yes;
}
bool vibe_audio_request(unsigned action) {
    if (!worker) return false;
    uint64_t now=vibe_now();
    portENTER_CRITICAL(&request_mux);
    requests|=action;if (action&V_START_AUDIO) voice_requested_at=now;
    portEXIT_CRITICAL(&request_mux);
    xTaskNotifyGive(worker); return true;
}
static bool radio_cancel(void) {
    vibe_model_t m; vibe_status_t s; vibe_snapshot(&m,NULL,&s);
    return pending() || !m.radio_active || !m.radio_requested || !s.peer || s.provisioning;
}
static void set_busy(bool yes) {
    xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.audio_busy=yes; xSemaphoreGive(v_mutex);
}
static bool prepare_audio(const char *operation) {
    esp_err_t result=bsp_audio_init();
    if (result==ESP_OK) result=bsp_audio_wake();
    xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.audio_fault=result!=ESP_OK; xSemaphoreGive(v_mutex);
    if (result!=ESP_OK) { ESP_LOGE(TAG,"%s preparation failed: %s",operation,esp_err_to_name(result));vibe_diagnostics_record(V_DIAG_AUDIO_PREPARE,result); }
    return result==ESP_OK;
}
static void alarm_play(int16_t *pcm) {
    vibe_prefs_t p; vibe_snapshot(NULL,&p,NULL);
    if (!prepare_audio("alarm")) return;
    if (bsp_audio_set_format(UI_SFX_RATE,16,1)!=ESP_OK) return;
    bsp_audio_set_volume(p.volume);
    unsigned total=ui_sfx_samples(UI_SFX_ALARM);
    for (unsigned pos=0;pos<total;) {
        if (pending()) { vibe_model_t m;vibe_snapshot(&m,NULL,NULL);if (m.recording) break; }
        unsigned count=total-pos<256 ? total-pos : 256;
        for (unsigned i=0;i<count;i++) pcm[i]=ui_sfx_sample(UI_SFX_ALARM,pos+i);
        if (bsp_audio_write(pcm,count*2)!=ESP_OK) break;
        pos+=count;
    }
}
static void capture(int16_t *pcm) {
    vibe_prefs_t p; vibe_model_t m; vibe_status_t s;
    vibe_snapshot(&m,&p,&s);
    if (!m.recording || !m.voice_ready) return;
    if (s.wifi && s.link==V_LINK_WIFI) esp_wifi_set_ps(WIFI_PS_NONE);
    bool started=false,failed=false;
    if (prepare_audio("capture") && bsp_audio_set_format(16000,16,1)==ESP_OK && bsp_audio_set_input_gain(p.mic_gain)==ESP_OK) {
        bsp_audio_set_volume(0); /* I2S duplex clocks remain active; no speaker feedback. */
        portENTER_CRITICAL(&request_mux);uint64_t requested=voice_requested_at;portEXIT_CRITICAL(&request_mux);
        if (requested) ESP_LOGI(TAG,"Voice capture ready: latency_ms=%llu",(unsigned long long)(vibe_now()-requested));
        if (!vibe_transport_voice_begin()) goto finished;
        started=true;
        /* Give the Mac time to press the shortcut before the first 100ms block. */
        vTaskDelay(pdMS_TO_TICKS(80));
        adpcm_state_t state; adpcm_state_reset(&state); uint8_t sequence=0;
        uint8_t packet[ADPCM_BLOCK_BYTES+2]; packet[1]=0x80;
        uint64_t start=vibe_now();
        for (;;) {
            vibe_snapshot(&m,NULL,&s);
            if (!m.recording || !m.voice_ready || vibe_now()-start>=180000) break;
            if (bsp_audio_read(pcm,ADPCM_BLOCK_SAMPLES*2)!=ESP_OK) { failed=true; break; }
            /* Keep the last block on normal release so word endings aren't cut. */
            size_t bytes=adpcm_encode_block(&state,pcm,ADPCM_BLOCK_SAMPLES,packet+2,ADPCM_BLOCK_BYTES);
            packet[0]=sequence++;
            if (bytes && vibe_send_audio(packet,bytes+2)!=0) {
                xSemaphoreTake(v_mutex,portMAX_DELAY); ++v_status.drops; xSemaphoreGive(v_mutex);
            }
        }
    } else failed=true;
finished:
    if (started) {
        /* This event and data use one socket; Mac has a 180ms audio-drain grace. */
        vibe_transport_voice_end();
    }
    if (failed) {
        ESP_LOGE(TAG,"Capture failed; next UP press may retry");
        vibe_diagnostics_record(V_DIAG_AUDIO_CAPTURE,0);
        vibe_msg_t msg={.type=V_MSG_AUDIO_FAIL}; xQueueSend(v_queue,&msg,pdMS_TO_TICKS(100));
    } else {
        xSemaphoreTake(v_mutex,portMAX_DELAY); v_model.recording=false; xSemaphoreGive(v_mutex);
    }
    if (s.wifi) esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}
static void process_audio(unsigned actions) {
    /* Join the optional weather worker before allocating/using audio resources. */
    vibe_weather_pause();
    if (actions&(V_START_AUDIO|V_ALARM|V_AGENT_ALERT)) {
        if (actions&V_START_AUDIO) { set_busy(true); capture(capture_pcm); }
        if (actions&(V_ALARM|V_AGENT_ALERT)) { set_busy(true); alarm_play(capture_pcm); }
    }
    unsigned failures=0;
    while (!radio_cancel()) {
        vibe_prefs_t p;vibe_snapshot(NULL,&p,NULL);
        set_busy(true);
        if (!prepare_audio("radio")) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);v_model.radio_requested=false;xSemaphoreGive(v_mutex);
            vibe_radio_status(V_RADIO_ERROR,"音频暂不可用");break;
        }
        char url[257];vibe_radio_url(url,sizeof(url));
        vibe_radio_status(V_RADIO_CONNECTING,"");
        uint64_t started=vibe_now();
        const vibe_radio_result_t result=vibe_relay_play(url,radio_cancel,capture_pcm);
        if (radio_cancel()) break;
        if (result==V_RADIO_SESSION_NO_MEMORY) {
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
        /* An established stream already spent time draining its FIFO and
         * timing out. Do not add two more silent seconds before its first
         * reconnect. Repeated short failures still back off and stop at three. */
        const unsigned wait_ms=failures==1?(result==V_RADIO_SESSION_PLAYED?0:2000):10000;
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(wait_ms));
    }
    esp_err_t result=bsp_audio_sleep();
    if (result!=ESP_OK) {
        ESP_LOGE(TAG,"Idle suspend failed: %s",esp_err_to_name(result));
        vibe_diagnostics_record(V_DIAG_AUDIO_SLEEP,result);
        xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.audio_fault=true;xSemaphoreGive(v_mutex);
    }
    set_busy(false);
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
    /* Voice is core functionality: reserve its small PCM block at startup,
     * including while radio owns its much larger, disposable workspace. */
    capture_pcm=malloc(ADPCM_BLOCK_SAMPLES*2);
    if (!capture_pcm) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task,"vibe_audio",10240,NULL,6,&worker)==pdPASS) return ESP_OK;
    free(capture_pcm);capture_pcm=NULL;worker=NULL;
    return ESP_ERR_NO_MEM;
}
