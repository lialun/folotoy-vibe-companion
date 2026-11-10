/* Exercise the production worker lifecycle: resource handoff must reclaim the
 * stack, not merely stop HTTP while a suspended task retains its allocation. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void release_body(void *body);
#define free release_body
#include "../../main/vibe/weather_worker.c"
#undef free

vibe_model_t v_model;
vibe_prefs_t v_prefs;
vibe_status_t v_status;
SemaphoreHandle_t v_mutex=(void *)1;
static bool locked,fail_create,fail_parse,change_city,cancel_fetch;
static uint64_t now=5000;
static unsigned creates,deletes,fetches,bodies,stack_bytes;
static void (*entry)(void *);
static jmp_buf parked;
static void release_body(void *body) {
    if (body) { assert(bodies==1);--bodies;free(body); }
}
static void run_worker(void) {
    assert(weather_worker && entry);
    if (!setjmp(parked)) entry(NULL);
}
uint64_t vibe_now(void) { return now; }
void vibe_snapshot(vibe_model_t *m,vibe_prefs_t *p,vibe_status_t *s) {
    if (m) *m=v_model;
    if (p) *p=v_prefs;
    if (s) *s=v_status;
}
int xSemaphoreTake(SemaphoreHandle_t semaphore,unsigned timeout) {
    (void)timeout;assert(semaphore==v_mutex && !locked);locked=true;return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    assert(semaphore==v_mutex && locked);locked=false;return pdTRUE;
}
int xTaskCreate(void (*f)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
    assert(locked && !stack_bytes && stack==8192 && priority==2 && !arg && !strcmp(name,"vibe_weather"));
    ++creates;if (fail_create) return 0;
    *handle=malloc(stack);assert(*handle);stack_bytes=stack;entry=f;return pdPASS;
}
void vTaskDelete(TaskHandle_t handle) {
    assert(locked && weather_finished && handle==weather_worker && stack_bytes==8192 && !bodies);
    free(handle);stack_bytes=0;++deletes;
}
void vTaskSuspend(TaskHandle_t handle) { assert(!handle && !locked && !bodies);longjmp(parked,1); }
void vTaskDelay(unsigned ticks) {
    assert(!locked && ticks==10 && weather_paused);
    /* A worker pending at audio takeover sees cancellation before allocating. */
    now+=ticks;run_worker();
}
char *vibe_weather_fetch(const char *url,bool (*allowed)(void),uint64_t (*clock_ms)(void)) {
    assert(allowed() && clock_ms()==now);++fetches;
    char *body=malloc(4);assert(body);memcpy(body,strstr(url,"air-quality")?"air":"sun",4);++bodies;
    if (change_city) strcpy(v_prefs.city,"Changed");
    if (cancel_fetch) v_model.recording=true;
    return body;
}
bool vibe_weather_parse(const char *body,vibe_weather_t *out) {
    assert(!strcmp(body,"sun") && bodies);
    *out=(vibe_weather_t){.valid=true,.hour_count=12};return !fail_parse;
}
bool vibe_air_parse(const char *body,vibe_air_t *out) {
    assert(!strcmp(body,"air") && bodies);
    *out=(vibe_air_t){.valid=true,.pm25_valid=true,.aqi_valid=true};return !fail_parse;
}
int main(void) {
    v_prefs.weather_enabled=true;strcpy(v_prefs.city,"Beijing");v_status.wifi=true;
    vibe_weather_poll(now);assert(creates==1 && stack_bytes==8192);
    vibe_weather_pause();assert(!weather_worker && !stack_bytes && deletes==1 && !fetches);
    now=10000;vibe_weather_poll(now);assert(creates==1);
    vibe_weather_resume();vibe_weather_poll(now);assert(creates==2);
    run_worker();assert(weather_finished && stack_bytes==8192 && fetches==2);
    vibe_weather_poll(++now);assert(!weather_worker && !stack_bytes && deletes==2);
    assert(v_status.weather_at_ms==10000 && v_status.air_at_ms==10000 && v_status.forecast.hour_count==12);
    now=20000;vibe_weather_poll(now);assert(creates==2);
    now=910000;fail_create=true;vibe_weather_poll(now);
    assert(creates==3 && !weather_worker && !stack_bytes);
    fail_create=false;vibe_weather_poll(now+100);assert(creates==3);
    now+=5000;vibe_weather_poll(now);assert(creates==4);
    fail_parse=true;run_worker();vibe_weather_pause();
    assert(!stack_bytes && v_status.weather_at_ms==10000 && v_status.air_at_ms==10000);
    fail_parse=false;vibe_weather_resume();now+=60000;change_city=true;
    vibe_weather_poll(now);run_worker();vibe_weather_pause();
    assert(!stack_bytes && v_status.weather_at_ms==10000 && v_status.air_at_ms==10000);
    change_city=false;vibe_weather_resume();
    now+=60000;cancel_fetch=true;unsigned before=fetches;
    vibe_weather_poll(now);run_worker();vibe_weather_pause();
    assert(fetches==before+1 && !bodies && !stack_bytes && v_status.weather_at_ms==10000);
    cancel_fetch=false;v_model.recording=false;vibe_weather_resume();
    for (unsigned i=0;i<100;i++) {
        now+=60000;v_status.weather_at_ms=v_status.air_at_ms=0;
        v_status.weather_attempt_ms=v_status.air_attempt_ms=0;
        vibe_weather_poll(now);run_worker();vibe_weather_pause();
        assert(!weather_worker && !stack_bytes && !bodies);vibe_weather_resume();
    }
    v_model.radio_requested=true;now+=900000;vibe_weather_poll(now);assert(!weather_worker);
    v_model.radio_requested=false;v_model.recording=true;vibe_weather_poll(now);assert(!weather_worker);
    v_model.recording=false;v_status.provisioning=true;vibe_weather_poll(now);assert(!weather_worker);
    v_status.provisioning=false;v_status.wifi=false;vibe_weather_poll(now);assert(!weather_worker);
    assert(!locked && deletes==creates-1);
    puts("Weather worker: on-demand stack, cancel/join before audio, cadence/backoff, stale-city rejection, allocation rollback and repeated cleanup PASS");
}
