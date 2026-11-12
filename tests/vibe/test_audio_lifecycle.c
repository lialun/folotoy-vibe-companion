/* Run the real application capture/alarm code with a sleeping codec. BSP tests
 * alone cannot detect a caller that forgets to wake before setting a format. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *test_pcm_allocate(size_t bytes);
static void test_pcm_release(void *ptr);
#define malloc test_pcm_allocate
#define free test_pcm_release
#include "../../main/vibe/audio.c"
#undef malloc
#undef free

vibe_model_t v_model;
vibe_prefs_t v_prefs;
vibe_status_t v_status;
SemaphoreHandle_t v_mutex=(void *)1;
QueueHandle_t v_queue=(void *)2;
void vibe_diagnostics_record(unsigned code,int32_t detail) { assert(code>=201 && code<=203);(void)detail; }
static bool initialized,sleeping,locked;
static bool fail_wake,fail_read,fail_radio_memory,reconnect_mode;
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
    (void)clear;assert(reconnect_mode && radio_wait_count<3);
    radio_waits[radio_wait_count++]=timeout;return 0;
}
void vTaskDelay(unsigned ticks) { now+=ticks; }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) { power_save=type; return ESP_OK; }
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
    v_model.recording=false; /* Physical release after one block. */
    return ESP_OK;
}
esp_err_t bsp_audio_write(const void *pcm,size_t bytes) {
    assert(!sleeping && pcm && bytes && bytes<=512); ++writes; return ESP_OK;
}
void vibe_send_json(const char *json) {
    if (strstr(json,"voice.start")) ++starts;
    else { assert(strstr(json,"voice.end")); ++ends; }
}
int vibe_send_audio(const void *bytes,size_t length) {
    assert(bytes && length==ADPCM_BLOCK_BYTES+2); ++packets; return 0;
}
bool vibe_transport_voice_begin(void) { starts++;return true; }
void vibe_transport_voice_end(void) { ends++; }
void vibe_radio_url(char *url,unsigned capacity) { snprintf(url,capacity,"http://test/radio"); }
void vibe_radio_status(unsigned state,const char *detail) { (void)detail; v_status.radio_state=state; }
unsigned ui_sfx_samples(ui_sfx_kind_t kind) { assert(kind==UI_SFX_ALARM); return 320; }
int16_t ui_sfx_sample(ui_sfx_kind_t kind,unsigned index) { assert(kind==UI_SFX_ALARM); return index%2 ? 300 : -300; }
int vibe_relay_play(const char *url,bool (*cancel)(void),int16_t *pcm) {
    assert(pcm==capture_pcm);
    (void)url;(void)cancel;assert(!sleeping && pcm_live==1 && background_paused);++radios;
    if (fail_radio_memory) { v_status.radio_state=V_RADIO_ERROR;strcpy(v_status.radio_error,"decoder memory");return V_RADIO_SESSION_NO_MEMORY; }
    if (reconnect_mode) { now+=1000;return V_RADIO_SESSION_PLAYED; }
    v_model.radio_requested=false;return V_RADIO_SESSION_PLAYED;
}

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
    v_model.voice_ready=true;v_status.wifi=true;v_status.peer=true;v_status.link=V_LINK_WIFI;
    assert(bsp_audio_init()==ESP_OK && bsp_audio_sleep()==ESP_OK);
    v_model.recording=true; capture(pcm);
    assert(starts==1 && ends==1 && packets==1 && !v_status.audio_fault);
    assert(power_save==WIFI_PS_MIN_MODEM);
    bsp_audio_sleep(); v_model.recording=true; capture(pcm);
    assert(starts==2 && ends==2 && packets==2 && wakes>=2);
    bsp_audio_sleep(); fail_wake=true; v_model.recording=true; capture(pcm);
    assert(starts==2 && ends==2 && failures==1 && v_status.audio_fault);
    fail_wake=false; v_model.recording=true; capture(pcm);
    assert(starts==3 && ends==3 && !v_status.audio_fault);
    bsp_audio_sleep(); fail_read=true; v_model.recording=true; capture(pcm);
    assert(starts==4 && ends==4 && failures==2 && v_status.audio_fault);
    fail_read=false; bsp_audio_sleep(); v_model.recording=true; capture(pcm);
    assert(starts==5 && ends==5 && packets==4 && !v_status.audio_fault);
    bsp_audio_sleep(); alarm_play(pcm);
    assert(writes==2);
    bsp_audio_sleep(); v_model.page=V_HOME;v_model.radio_active=true; v_model.radio_requested=true; v_status.wifi=true;
    process_audio(V_RADIO_CHANGED);
    assert(radios==1 && sleeping && !v_status.audio_busy);
    v_model.page=V_HOME; v_model.recording=true;
    process_audio(V_START_AUDIO);
    assert(starts==6 && ends==6 && sleeping && !v_status.audio_busy);
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
    assert(starts==10 && ends==10 && pcm_live==1 && pcm_peak==1 && !background_paused);
    test_pcm_release(capture_pcm);capture_pcm=NULL;
    puts("Application audio: resident voice buffer, boot rollback, capture under heap pressure, wake/read retry and radio lifecycle PASS");
}
