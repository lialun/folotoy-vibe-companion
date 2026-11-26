/* Execute the production remote player, with real IMA and simulated I2S time.
 * In particular, writing a block advances time; checking an older unsigned
 * timestamp afterward must never turn normal playback into an idle timeout. */
#include "vibe.h"
#include "radio/stream.h"
#include "esp_wifi.h"
#include "wifi_power.h"
#include "bsp_audio.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
SemaphoreHandle_t xSemaphoreCreateMutex(void);
static void *allocate(size_t),release(void *);
#define malloc allocate
#define calloc(count,size) allocate((count)*(size))
#define free release
#define vibe_radio_session play_remote_session
#include "../../main/vibe/radio/remote.c"
#undef vibe_radio_session
#undef malloc
#undef calloc
#undef free
vibe_model_t v_model;vibe_prefs_t v_prefs;vibe_status_t v_status;
SemaphoreHandle_t v_mutex=(void *)1;QueueHandle_t v_queue;
static void *owned[V_RADIO_WINDOW+2];
static size_t allocation_limit;
static unsigned live,allocations,fail_allocation,writes,target,opens,closes,acks;
static int16_t shared_pcm[V_RADIO_SAMPLES];
static bool ingress;
static bool lock,feed,format_fail,write_fail,source_fail,switch_owner;
static uint64_t clock_ms;
static uint32_t owner=1,produced;
static wifi_ps_type_t ps=WIFI_PS_MIN_MODEM;
static vibe_wifi_power_t network_policy;
static bool network_effect,voice_tail;
static esp_coex_prefer_t coex=ESP_COEX_PREFER_BALANCE;static unsigned coex_calls;static bool coex_fail;
static unsigned jitter_ms,bufferings;static uint64_t refill_at;
static void fill(void);
static void *allocate(size_t size) { if (++allocations==fail_allocation || (allocation_limit && size>allocation_limit)) return NULL;void *p=malloc(size);assert(p);memset(p,0,size);assert(live<V_RADIO_WINDOW+2);owned[live++]=p;return p; }
static void release(void *p) { if (p) { for(unsigned i=0;i<live;i++)if(owned[i]==p){owned[i]=owned[--live];break;} free(p); } }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)2; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) { (void)timeout;assert((s==mutex || s==v_mutex) && !lock);lock=true;return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t s) { assert((s==mutex || s==v_mutex) && lock);lock=false;return pdTRUE; }
uint64_t vibe_now(void) { return clock_ms; }
void vTaskDelay(unsigned ticks) { clock_ms+=ticks;if(refill_at && clock_ms>=refill_at){refill_at=0;fill();} }
void esp_fill_random(void *p,size_t n) { memset(p,0x12,n); }
uint32_t vibe_network_generation(void) { return owner; }
bool vibe_network_generation_matches(uint32_t generation) { return generation==owner; }
void vibe_snapshot(vibe_model_t *m,vibe_prefs_t *p,vibe_status_t *s) { if(m)*m=v_model;if(p)*p=v_prefs;if(s)*s=v_status; }
void vibe_radio_status(unsigned state,const char *detail) { (void)detail;v_status.radio_state=state;if(state==V_RADIO_BUFFERING)++bufferings; }
void vibe_radio_played(const char *url) { assert(!strcmp(url,"http://test/audio")); }
esp_err_t esp_wifi_set_ps(wifi_ps_type_t mode) {assert(network_effect);ps=mode;return ESP_OK;}
static void network_radio(bool active) {
    vibe_wifi_power_action_t action=vibe_wifi_power_next(&network_policy,true,false,voice_tail,active);
    if(action==V_WIFI_POWER_UNCHANGED)return;
    network_effect=true;esp_wifi_set_ps(action==V_WIFI_POWER_AWAKE?WIFI_PS_NONE:WIFI_PS_MIN_MODEM);network_effect=false;
}
vibe_radio_result_t vibe_radio_session(const char *url,bool (*cancelled)(void),int16_t *pcm) {
    network_radio(true);
    vibe_radio_result_t result=play_remote_session(url,cancelled,pcm);
    network_radio(false);
    return result;
}
esp_err_t esp_coex_preference_set(esp_coex_prefer_t preference) {
    assert(!lock);++coex_calls;
    if(coex_fail && preference==ESP_COEX_PREFER_BT)return ESP_ERR_NOT_SUPPORTED;
    coex=preference;return ESP_OK;
}
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels) { assert(hz==V_RADIO_RATE && bits==16 && channels==1);return format_fail?ESP_FAIL:ESP_OK; }
void bsp_audio_set_volume(uint8_t volume) { assert(volume==0); }
esp_err_t bsp_audio_write(const void *data,size_t n) { assert(data && n==V_RADIO_SAMPLES*2 && ps==WIFI_PS_NONE && (coex==ESP_COEX_PREFER_BT || coex_fail));++writes;clock_ms+=100;if(jitter_ms && writes==2){stream->mask=0;produced=stream->next;refill_at=clock_ms+jitter_ms;}if(switch_owner)++owner;return write_fail?ESP_FAIL:ESP_OK; }
static void fill(void) {
    if (!feed || !stream) return;
    while(produced-stream->next<V_RADIO_WINDOW) {
        uint8_t packet[V_RADIO_PACKET]={5};packet[13]=V_RADIO_FORMAT;
        for(unsigned i=0;i<8;i++)packet[1+i]=(uint8_t)(stream->session>>(i*8));
        for(unsigned i=0;i<4;i++)packet[9+i]=(uint8_t)(produced>>(i*8));
        packet[14]=(uint8_t)produced;
        vibe_radio_remote_receive(packet,sizeof(packet),owner,produced%2);++produced;
    }
}
void vibe_send_json_both(const char *json) {
    assert(!lock);cJSON *o=cJSON_Parse(json);assert(o);const char *event=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,"event"));assert(event);
    if(!strcmp(event,"radio.open")) {assert(cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(o,"format"))==V_RADIO_FORMAT);++opens;fill();if(source_fail)vibe_radio_remote_status(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,"session")),true);}
    else if(!strcmp(event,"radio.progress")) {++acks;fill();}
    else {assert(!strcmp(event,"radio.close"));++closes;}
    cJSON_Delete(o);
}
static bool cancel(void) { return writes>=target; }
static void reset(void) {assert(!live && !stream && !ingress && !lock && coex==ESP_COEX_PREFER_BALANCE);coex_calls=0;coex_fail=false;allocation_limit=0;allocations=fail_allocation=writes=opens=closes=acks=produced=0;clock_ms=100;target=20;jitter_ms=bufferings=0;refill_at=0;feed=true;format_fail=write_fail=source_fail=switch_owner=false;}
int main(void) {
    assert(vibe_radio_remote_init());
    for(unsigned n=1;n<=1;n++) {reset();fail_allocation=n;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_NO_MEMORY);assert(!live && !stream);}
    for(unsigned cycle=0;cycle<100;cycle++) {reset();assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_PLAYED);assert(writes==20 && opens==1 && closes==1 && acks>=20 && !live && !stream && ps==WIFI_PS_MIN_MODEM && coex==ESP_COEX_PREFER_BALANCE && coex_calls==2);}
    reset();voice_tail=true;
    assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_NO_MEMORY && ps==WIFI_PS_NONE);
    voice_tail=false;network_radio(false);assert(ps==WIFI_PS_MIN_MODEM);
    reset();allocation_limit=20000;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_PLAYED);assert(writes==20 && !live);
    reset();coex_fail=true;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_PLAYED);assert(coex==ESP_COEX_PREFER_BALANCE && coex_calls==2);
    reset();jitter_ms=30;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_PLAYED);assert(writes==20 && bufferings==0 && !live);
    reset();jitter_ms=80;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_PLAYED);assert(writes==20 && bufferings==1 && !live);
    reset();feed=false;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_REMOTE_ERROR);assert(!writes && clock_ms<=20200 && opens<=21 && closes==1 && !live);
    reset();source_fail=true;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_REMOTE_ERROR);assert(!live && closes==1);
    reset();write_fail=true;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_REMOTE_ERROR);assert(writes==1 && !live);
    reset();format_fail=true;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_REMOTE_ERROR);assert(!writes && !live);
    reset();switch_owner=true;assert(vibe_radio_session("http://test/audio",cancel,shared_pcm)==V_RADIO_SESSION_PLAYED);assert(writes==1 && !live && !stream);
    assert(coex==ESP_COEX_PREFER_BALANCE);
    puts("Production remote player: 100 sessions, scoped coexistence/unsupported hint, 30/80 ms ingress jitter, actual clock/I2S progression, credits, both routes, timeout, source/write/format/allocation faults, handover and cleanup PASS");
}

bool vibe_media_acquire(vibe_media_owner_t requested,size_t size,vibe_media_lease_t *lease) {
    assert(requested==V_MEDIA_RADIO && !live);
    if(voice_tail)return false;
    void *memory=allocate(size);if(!memory)return false;
    *lease=(vibe_media_lease_t){memory,size,1,V_MEDIA_RADIO};return true;
}
void vibe_media_release(vibe_media_lease_t lease) {assert(lease.owner==V_MEDIA_RADIO && live==1 && !stream);release(lease.memory);}
void vibe_ble_radio_ingress(bool active) {ingress=active;}
