/* Real relay lifecycle, deterministic clock/producer and allocator faults.
 * ASan checks repeated cleanup; lock assertions catch callbacks under locks. */
#include "vibe.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
SemaphoreHandle_t xSemaphoreCreateMutex(void);
static void *allocate(size_t n);
static void release(void *p);
#define malloc(n) allocate(n)
#define free(p) release(p)
#include "../../main/vibe/radio/relay.c"
#undef malloc
#undef free
vibe_model_t v_model;
vibe_prefs_t v_prefs;
vibe_status_t v_status;
SemaphoreHandle_t v_mutex=(void *)1;
QueueHandle_t v_queue;
static bool locked[3],cancelled;
static void *owned[3];
static unsigned allocations,writes,opens,closes,errors,mode,volume;
static uint64_t now;
static unsigned diagnostic;
void vibe_diagnostics_record(unsigned code,int32_t detail) { assert(code>=101 && code<=105);diagnostic=code;(void)detail; }
static vibe_route_t route={.kind=V_LINK_BLE,.handle=7,.serial=9};
static void *allocate(size_t n) {
    if ((mode==8 && allocations==2) || (mode==9 && allocations==1)) return NULL;
    void *p=malloc(n);assert(p);for (unsigned i=0;i<3;i++) if (!owned[i]) { owned[i]=p;allocations++;return p; }abort();
}
static void release(void *p) { if (p) { for (unsigned i=0;i<3;i++) if (owned[i]==p) { assert(allocations);--allocations;owned[i]=NULL;break; }free(p); } }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)2; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) { (void)timeout;unsigned i=(unsigned)(uintptr_t)s;assert(i<3 && !locked[i]);locked[i]=true;return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { unsigned i=(unsigned)(uintptr_t)s;assert(locked[i]);locked[i]=false;return 1; }
uint64_t vibe_now(void) { return now; }
uint32_t esp_random(void) { return 777; }
size_t heap_caps_get_free_size(uint32_t caps) { (void)caps;return (mode==1?20000:mode==2?40000:100000)-allocations*7852; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { return heap_caps_get_free_size(caps); }
vibe_route_t vibe_transport_route(void) { return route; }
bool vibe_route_equal(vibe_route_t a,vibe_route_t b) { return !memcmp(&a,&b,sizeof(a)); }
void vibe_snapshot(vibe_model_t *m,vibe_prefs_t *p,vibe_status_t *s) { (void)m;(void)s;if (p) p->volume=writes>20?30:55; }
void vibe_radio_status(unsigned s,const char *detail) { (void)detail;if (s==V_RADIO_ERROR) errors++; }
void vibe_radio_played(const char *url) { assert(!strcmp(url,"https://example.org/live")); }
esp_err_t bsp_audio_set_format(uint32_t rate,uint8_t bits,uint8_t channels) { assert(rate==24000 && bits==16 && channels==1);return 0; }
void bsp_audio_set_volume(uint8_t v) { volume=v; }
bool cancel(void) { return cancelled; }
esp_err_t bsp_audio_write(const void *pcm,size_t bytes) {
    assert(pcm && bytes==2400);++writes;now+=50;
    assert(volume==(writes>21?30U:55U));
    if (writes==5) {
        if (mode==3) vibe_relay_abort();
        if (mode==4) route.serial++;
        if (mode==6) { cJSON *o=cJSON_Parse("{\"stream\":777}");vibe_relay_control(o);cJSON_Delete(o); }
    }
    if (writes==120) cancelled=true;
    return 0;
}
unsigned ulTaskNotifyTake(int clear,unsigned timeout) { (void)clear;now+=timeout;return 0; }
static void number(uint8_t *p,uint32_t n) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
void vibe_send_json(const char *line) {
    assert(!locked[1] && !locked[2]);
    cJSON *o=cJSON_Parse(line);assert(o);const char *kind=cJSON_GetObjectItem(o,"event")->valuestring;
    if (!strcmp(kind,"relay.close")) { closes++;cJSON_Delete(o);return; }
    if (!strcmp(kind,"relay.open")) opens++;
    uint32_t next=(uint32_t)cJSON_GetObjectItem(o,"next")->valuedouble,limit=(uint32_t)cJSON_GetObjectItem(o,"limit")->valuedouble;
    if (mode!=7) for (uint32_t i=next;i<limit;i++) {
        uint8_t p[V_RELAY_PACKET]={5};number(p+1,777);number(p+5,i);
        vibe_route_t wrong=route;wrong.serial++;
        assert(!vibe_relay_receive(wrong,p,sizeof(p)));
        if (mode==5 && i==next) { number(p+5,i+1);assert(!vibe_relay_receive(route,p,sizeof(p)));number(p+5,i); }
        (void)vibe_relay_receive(route,p,sizeof(p));
    }
    cJSON_Delete(o);
}
int main(void) {
    assert(vibe_relay_init()==0);
    for (unsigned repeat=0;repeat<10;repeat++) for (mode=0;mode<10;mode++) {
        cancelled=false;writes=opens=closes=errors=diagnostic=0;now=1;route.serial=9;
        int16_t pcm[1600];int r=vibe_relay_play("https://example.org/live",cancel,pcm);
        assert(!allocations && !buffer.capacity && !source.kind && !locked[1] && !locked[2]);
        if (mode==1 || mode==9) { assert(r==V_RADIO_SESSION_NO_MEMORY && errors==1 && !opens && !closes && diagnostic==V_DIAG_RADIO_MEMORY);continue; }
        assert(opens && closes==1);
        if (mode==7) assert(!writes && errors==1 && now>=20000 && diagnostic==V_DIAG_RADIO_TIMEOUT);
        else assert(r==V_RADIO_SESSION_PLAYED && writes==(mode==3 || mode==4 || mode==6?5U:120U));
        if (mode==0 || mode==2 || mode==5) assert(!errors && cancelled && !diagnostic);
        if (mode==6) assert(diagnostic==V_DIAG_RADIO_REMOTE);
    }
    puts("Real relay session: partial allocation fallback/reserve, wrong route, gap repair, live volume, abort/handoff/error/timeout and 100 cleanup cycles PASS");
}
