#include "media_workspace.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static size_t available=40000,largest=30000;
static bool locked;
static size_t allocated,allocation_pressure;
static void *allocate(size_t bytes);
static void release(void *memory);
#define malloc allocate
#define free release
#include "../../main/vibe/media_workspace.c"
#undef malloc
#undef free
static void *allocate(size_t bytes) {
    assert(!allocated);available-=allocation_pressure;allocation_pressure=0;
    void *memory=malloc(bytes);if(memory)allocated=bytes;return memory;
}
static void release(void *memory) {if(memory){assert(allocated);free(memory);allocated=0;}}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return (void *)1;}
int xSemaphoreTake(SemaphoreHandle_t value,unsigned timeout) {(void)timeout;assert(value==mutex && !locked);locked=true;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t value) {assert(value==mutex && locked);locked=false;return pdTRUE;}
size_t heap_caps_get_free_size(unsigned caps) {(void)caps;assert(available>=allocated);return available-allocated;}
size_t heap_caps_get_largest_free_block(unsigned caps) {(void)caps;return largest;}
int main(void) {
    assert(vibe_media_init()==ESP_OK && frames==20);
    void *original=arena;vibe_media_lease_t radio,voice_lease;
    assert(vibe_media_owner()==V_MEDIA_IDLE);
    assert(vibe_media_acquire(V_MEDIA_RADIO,12960,&radio));
    assert(!vibe_media_acquire(V_MEDIA_VOICE,5040,&voice_lease));
    vibe_media_lease_t stale=radio;stale.generation++;
    vibe_media_release(stale);assert(vibe_media_owner()==V_MEDIA_RADIO);
    vibe_media_release(radio);assert(vibe_media_owner()==V_MEDIA_IDLE);
    unsigned count=0;vibe_voice_tx_slot_t *slots=vibe_media_voice_acquire(&count);
    assert(slots==original && count==20 && vibe_media_owner()==V_MEDIA_VOICE);
    assert(!vibe_media_acquire(V_MEDIA_RADIO,12960,&radio));
    vibe_media_release(stale);assert(vibe_media_owner()==V_MEDIA_VOICE);
    vibe_media_voice_release(slots);assert(vibe_media_owner()==V_MEDIA_IDLE);
    assert(vibe_media_acquire(V_MEDIA_RADIO,12960,&radio) && radio.memory==original);
    vibe_media_release(radio);release(arena);arena=NULL;frames=0;
    available=48000;largest=28000;
    assert(vibe_media_init()==ESP_OK && frames==30);
    release(arena);arena=NULL;frames=0;available=17000;
    assert(vibe_media_init()==ESP_ERR_NO_MEM && !arena);
    available=40000;largest=30000;allocation_pressure=4000;
    assert(vibe_media_init()==ESP_OK && frames==16);
    assert(heap_caps_get_free_size(0)>=V_VOICE_HEAP_RESERVE);
    release(arena);arena=NULL;frames=0;
    puts("Media arena: fixed reserve, contiguous bounds, exclusive leases, stale release and priority reuse PASS");
}
