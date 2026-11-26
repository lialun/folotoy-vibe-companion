/* Run the real application capture/alarm code with a sleeping codec. BSP tests
 * alone cannot detect a caller that forgets to wake before setting a format. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "wifi_power.h"
static void *test_pcm_allocate(size_t bytes);
static void test_pcm_release(void *ptr);
#define malloc test_pcm_allocate
#define free test_pcm_release
#include "../../main/vibe/audio.c"
#define vibe_voice_capture capture_on_audio_worker
#include "../../main/vibe/voice_capture.c"
#undef vibe_voice_capture
#undef malloc
#undef free

vibe_model_t v_model;
vibe_prefs_t v_prefs;
vibe_status_t v_status;
SemaphoreHandle_t v_mutex=(void *)1;
QueueHandle_t v_queue=(void *)2;
static bool initialized,sleeping,locked;
static bool fail_wake,fail_read,fail_radio_memory,reconnect_mode;
static bool ble_streaming;
static vibe_media_owner_t media_owner;
static unsigned tail_waits;
static bool press_during_tail;
void vibe_ble_streaming(bool active) {ble_streaming=active;}
static bool replace_during_read,ready_reply=true,change_owner_during_read;
static uint32_t generation=7;
static unsigned stop_reason;
static vibe_voice_mode_t last_voice_mode;
static unsigned radio_waits[3],radio_wait_count;
static unsigned pcm_live,pcm_peak;
static bool fail_pcm_allocate,fail_worker_create,background_paused;
static void *test_pcm_allocate(size_t bytes) {
    assert(bytes==ADPCM_BLOCK_SAMPLES*2);
    if (fail_pcm_allocate) return NULL;
    void *p=malloc(bytes);assert(p);++pcm_live;if (pcm_live>pcm_peak) pcm_peak=pcm_live;return p;
}
static void test_pcm_release(void *ptr) { assert(ptr && pcm_live==1);--pcm_live;free(ptr); }
static unsigned starts,ends,packets,writes,failures,wakes,radios;
static uint64_t now;
static wifi_ps_type_t power_save=WIFI_PS_MIN_MODEM;
static vibe_wifi_power_t network_power;
static bool network_uplink,network_effect,radio_intent;
static uint64_t network_release_at;
static void network_power_tick(void) {
    if(network_release_at && now>=network_release_at)network_uplink=false;
    vibe_ble_streaming(network_uplink || radio_intent);
    vibe_wifi_power_action_t action=vibe_wifi_power_next(&network_power,true,false,network_uplink,radio_intent);
    if(action==V_WIFI_POWER_UNCHANGED)return;
    network_effect=true;
    esp_wifi_set_ps(action==V_WIFI_POWER_AWAKE?WIFI_PS_NONE:WIFI_PS_MIN_MODEM);
    network_effect=false;
}
void vibe_network_radio_activity(bool active) {radio_intent=active;network_power_tick();}
void vibe_voice_capture(int16_t *pcm) {
    capture_on_audio_worker(pcm);
    if(network_uplink)assert(power_save==WIFI_PS_NONE); /* Tail is still live. */
    now+=V_VOICE_RETAIN_MS+1;network_power_tick();
}
uint32_t esp_random(void) {static uint32_t nonce=1;return nonce++;}
uint32_t vibe_network_generation(void) {return generation;}
void vibe_voice_report(void) {assert(v_status.voice_v2);}
vibe_voice_counters_t vibe_voice_counters(void) {return (vibe_voice_counters_t){0};}
unsigned vibe_voice_begin(uint64_t session,uint32_t owner,vibe_voice_mode_t mode,bool feedback){(void)feedback;last_voice_mode=mode;assert(session && owner==7);network_uplink=true;network_release_at=0;network_power_tick();return 4;}
unsigned vibe_voice_links(uint32_t owner){assert(owner==7);return 1;}
void vibe_voice_end(uint64_t session,uint32_t owner){assert(session && owner==7);network_release_at=now+V_VOICE_RETAIN_MS;}
bool vibe_voice_uplink_is_ready(uint64_t session,uint32_t owner){assert(session && owner==7);return ready_reply;}
bool vibe_voice_uplink_cancelled(uint64_t session,uint32_t owner){assert(session && owner==7);return false;}

uint64_t vibe_now(void) { return now; }
void vibe_snapshot(vibe_model_t *m,vibe_prefs_t *p,vibe_status_t *s) {
    if (m) *m=v_model;
    if (p) *p=v_prefs;
    if (s) *s=v_status;
}
int xSemaphoreTake(SemaphoreHandle_t semaphore,unsigned timeout) {
    (void)timeout; assert(semaphore==v_mutex && !locked); locked=true; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    assert(semaphore==v_mutex && locked); locked=false; return pdTRUE;
}
int xQueueSend(QueueHandle_t queue,const void *item,unsigned timeout) {
    (void)timeout; assert(queue==v_queue);
    const vibe_msg_t *message=item; assert(message->type==V_MSG_AUDIO_FAIL);
    ++failures; v_model.recording=false; v_status.audio_fault=true; return pdTRUE;
}
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)entry;(void)name;(void)stack;(void)arg;(void)priority;
    if (fail_worker_create) return 0;
    *handle=(void *)3; return pdPASS;
}
void vibe_weather_pause(void) { assert(!background_paused);background_paused=true; }
void vibe_weather_resume(void) { assert(background_paused);background_paused=false; }
void vTaskDelete(TaskHandle_t task) { (void)task; abort(); }
void xTaskNotifyGive(TaskHandle_t task) { assert(task); }
unsigned ulTaskNotifyTake(int clear,unsigned timeout) {
    (void)clear;
    if(media_owner==V_MEDIA_VOICE) {
        assert(timeout==20 && !radio_intent && ble_streaming && network_uplink);
        ++tail_waits;
        if(press_during_tail)requests|=V_START_AUDIO;
        else if(tail_waits==2){media_owner=V_MEDIA_IDLE;network_uplink=false;network_power_tick();}
        return 0;
    }
    assert(reconnect_mode && radio_wait_count<3 && !ble_streaming && !radio_intent);
    radio_waits[radio_wait_count++]=timeout;return 0;
}
void vTaskDelay(unsigned ticks) { now+=ticks; }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) { assert(network_effect);power_save=type;return ESP_OK; }
esp_err_t bsp_audio_init(void) { initialized=true; return ESP_OK; }
esp_err_t bsp_audio_sleep(void) { sleeping=true; return ESP_OK; }
esp_err_t bsp_audio_wake(void) {
    ++wakes; assert(initialized);
    if (fail_wake) return ESP_FAIL;
    sleeping=false; return ESP_OK;
}
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels) {
    assert(hz==16000 && bits==16 && channels==1);
    return initialized && !sleeping ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t bsp_audio_set_input_gain(uint8_t gain) { assert(!sleeping && gain==30); return ESP_OK; }
void bsp_audio_set_volume(uint8_t volume) { assert(!sleeping && volume<=100); }
esp_err_t bsp_audio_read(void *pcm,size_t bytes) {
    assert(!sleeping && bytes==ADPCM_BLOCK_SAMPLES*2 && power_save==WIFI_PS_NONE);
    if (fail_read) return ESP_FAIL;
    int16_t *samples=pcm;
    for (size_t i=0;i<bytes/2;i++) samples[i]=i%2 ? 1000 : -1000;
    if (change_owner_during_read) {++generation;change_owner_during_read=false;}
    else if (replace_during_read) {++v_model.voice_request;replace_during_read=false;}
    else v_model.recording=false; /* Physical release after one block. */
    return ESP_OK;
}
esp_err_t bsp_audio_write(const void *pcm,size_t bytes) {
    assert(!sleeping && pcm && bytes && bytes<=512); ++writes; return ESP_OK;
}
void vibe_send_json(const char *json) {
    if (strstr(json,"voice.stats")) {assert(strstr(json,"\"captureGapMs\":") && strstr(json,"\"tx\":["));return;}
    if (strstr(json,"voice.start")) ++starts;
    else {
        assert(strstr(json,"voice.end"));
        assert(!strstr(json,"\"tx\":[")); /* Critical end stays compact. */
        const char *stop=strstr(json,"\"stop\":");
        if (stop) stop_reason=(unsigned)strtoul(stop+7,NULL,10);
        ++ends;
    }
}
void vibe_send_json_owned(const char *json,uint32_t owner,vibe_delivery_t delivery) {assert(owner==7);assert(delivery==(strstr(json,"voice.stats")?V_DELIVERY_BOTH:V_DELIVERY_CRITICAL));vibe_send_json(json); }
int vibe_send_voice(const void *bytes,size_t length,uint32_t owner) {
    const uint8_t *p=bytes;assert(owner==7 && length==VIBE_VOICE_BODY);
    assert(p[0] && p[8]==0 && p[9]==0 && p[10]==0 && p[11]==0);++packets;return 0;
}
void vibe_radio_url(char *url,unsigned capacity) { snprintf(url,capacity,"http://test/radio"); }
void vibe_radio_status(unsigned state,const char *detail) { (void)detail; v_status.radio_state=state; }
unsigned ui_sfx_samples(ui_sfx_kind_t kind) { assert(kind==UI_SFX_ALARM); return 320; }
int16_t ui_sfx_sample(ui_sfx_kind_t kind,unsigned index) { assert(kind==UI_SFX_ALARM); return index%2 ? 300 : -300; }
vibe_radio_result_t vibe_radio_session(const char *url,bool (*cancel)(void),int16_t *pcm) {
    assert(pcm==capture_pcm);
    network_power_tick();assert(power_save==WIFI_PS_NONE);
    (void)url;(void)cancel;assert(!sleeping && pcm_live==1 && background_paused && ble_streaming);++radios;
    if (fail_radio_memory) { v_status.radio_state=V_RADIO_ERROR;strcpy(v_status.radio_error,"decoder memory");return V_RADIO_SESSION_NO_MEMORY; }
    if (reconnect_mode) { now+=1000;return V_RADIO_SESSION_PLAYED; }
    v_model.radio_requested=false;return V_RADIO_SESSION_PLAYED;
}

bool vibe_radio_remote_init(void) { return true; }
int main(void) {
    fail_pcm_allocate=true;
    assert(vibe_audio_init()==ESP_ERR_NO_MEM && !worker && !capture_pcm && !pcm_live);
    fail_pcm_allocate=false;fail_worker_create=true;
    assert(vibe_audio_init()==ESP_ERR_NO_MEM && !worker && !capture_pcm && !pcm_live);
    fail_worker_create=false;
    assert(vibe_audio_init()==ESP_OK && worker && capture_pcm && pcm_live==1);
    assert(vibe_audio_init()==ESP_OK && pcm_live==1);
    /* Even under later heap pressure, voice reuses its guaranteed boot block. */
    fail_pcm_allocate=true;
    int16_t pcm[ADPCM_BLOCK_SAMPLES];
    v_prefs=(vibe_prefs_t){.mic_gain=30,.volume=80};
    v_model.voice_ready=true;
    assert(bsp_audio_init()==ESP_OK && bsp_audio_sleep()==ESP_OK);
    v_model.recording=true; vibe_voice_capture(pcm);
    assert(starts==1 && ends==1 && packets==1 && !v_status.audio_fault);
    assert(power_save==WIFI_PS_MIN_MODEM);
    bsp_audio_sleep(); v_model.recording=true; vibe_voice_capture(pcm);
    assert(starts==2 && ends==2 && packets==2 && wakes>=2);
    bsp_audio_sleep(); fail_wake=true; v_model.recording=true; vibe_voice_capture(pcm);
    assert(starts==2 && ends==2 && failures==1 && v_status.audio_fault);
    fail_wake=false; v_model.recording=true; vibe_voice_capture(pcm);
    assert(starts==3 && ends==3 && !v_status.audio_fault);
    bsp_audio_sleep(); fail_read=true; v_model.recording=true; vibe_voice_capture(pcm);
    assert(starts==4 && ends==4 && failures==2 && v_status.audio_fault);
    fail_read=false; bsp_audio_sleep(); v_model.recording=true; vibe_voice_capture(pcm);
    assert(starts==5 && ends==5 && packets==4 && !v_status.audio_fault);
    bsp_audio_sleep(); alarm_play(pcm);
    assert(writes==2);
    bsp_audio_sleep(); v_model.page=V_HOME;v_model.radio_active=true; v_model.radio_requested=true; v_status.wifi=true;v_status.peer=true;v_status.radio_pcm=true;
    process_audio(V_RADIO_CHANGED);
    assert(radios==1 && sleeping && !v_status.audio_busy && !ble_streaming);
    v_model.page=V_HOME; v_model.recording=true;
    process_audio(V_START_AUDIO);
    assert(starts==6 && ends==6 && sleeping && !v_status.audio_busy && !ble_streaming);
    v_model.radio_requested=true;v_model.recording=true;
    process_audio(V_START_AUDIO);
    assert(starts==7 && ends==7 && radios==2 && sleeping && !v_status.audio_busy);
    v_model.recording=true; /* Paused listening must not resume after speech. */
    process_audio(V_START_AUDIO);
    assert(starts==8 && ends==8 && radios==2 && sleeping);
    fail_radio_memory=true;v_model.radio_requested=true;
    process_audio(V_RADIO_STATION);
    assert(radios==3 && !v_model.radio_requested && sleeping && !v_status.audio_busy);
    assert(v_status.radio_state==V_RADIO_ERROR && !strcmp(v_status.radio_error,"decoder memory"));
    fail_radio_memory=false;reconnect_mode=true;v_model.radio_requested=true;
    process_audio(V_RADIO_CHANGED);
    assert(radios==6 && !v_model.radio_requested && sleeping);
    assert(radio_wait_count==2 && radio_waits[0]==0 && radio_waits[1]==10000);
    reconnect_mode=false;v_model.recording=true;
    process_audio(V_START_AUDIO);
    assert(failures==2 && !v_model.recording && pcm_live==1 && starts==9);
    v_model.recording=true;process_audio(V_START_AUDIO);
    assert(starts==10 && ends==10 && pcm_live==1 && pcm_peak==1 && !background_paused && !radio_intent);
    v_status.voice_v2=true;v_model.recording=true;process_audio(V_START_AUDIO);
    assert(starts==11 && ends==11 && pcm_live==1 && pcm_peak==1 && !background_paused);
    v_model.recording=true;v_model.voice_request=100;replace_during_read=true;
    process_audio(V_START_AUDIO);
    assert(v_model.recording && v_model.voice_request==101 && stop_reason==4);
    process_audio(V_START_AUDIO);
    assert(!v_model.recording && stop_reason==0);
    v_status.voice_handshake=true;ready_reply=false;v_model.recording=true;
    unsigned previous_packets=packets;
    process_audio(V_START_AUDIO);
    assert(stop_reason==6 && packets==previous_packets && !v_model.recording);
    assert(last_voice_mode==V_VOICE_V2); /* Readiness does not imply interleaved assembly. */
    ready_reply=true;v_status.voice_interleave=true;v_model.recording=true;
    process_audio(V_START_AUDIO);assert(last_voice_mode==V_VOICE_STREAM && stop_reason==0);
    v_status.voice_handshake=false;v_model.recording=true;
    process_audio(V_START_AUDIO);assert(last_voice_mode==V_VOICE_STREAM && stop_reason==0);
    v_status.voice_interleave=false;v_status.voice_handshake=true;v_model.recording=true;
    process_audio(V_START_AUDIO);assert(last_voice_mode==V_VOICE_V2 && stop_reason==0);
    for(unsigned modern=0;modern<2;modern++) {
        v_status.voice_v2=modern;v_status.voice_handshake=false;v_model.recording=true;change_owner_during_read=true;
        process_audio(V_START_AUDIO);assert(!v_model.recording && generation==8 && (!modern || stop_reason==1));
        generation=7;
    }
    /* Radio must not borrow a live voice tail, and a new press must escape
     * the wait rather than sit behind radio restart. */
    unsigned before_radio=radios;
    v_model.radio_requested=true;media_owner=V_MEDIA_VOICE;network_uplink=true;network_release_at=0;network_power_tick();
    process_audio(V_RADIO_CHANGED);
    assert(tail_waits==2 && radios==before_radio+1 && sleeping);
    tail_waits=0;press_during_tail=true;media_owner=V_MEDIA_VOICE;
    network_uplink=true;network_power_tick();v_model.radio_requested=true;
    process_audio(V_RADIO_CHANGED);
    assert(tail_waits==1 && radios==before_radio+1 && pending() && ble_streaming);
    assert(take_requests()==V_START_AUDIO);
    media_owner=V_MEDIA_IDLE;network_uplink=false;network_power_tick();
    test_pcm_release(capture_pcm);capture_pcm=NULL;
    puts("Application audio: capture request replacement, handshake timeout, boot rollback and radio lifecycle PASS");
}

vibe_media_owner_t vibe_media_owner(void) {return media_owner;}
