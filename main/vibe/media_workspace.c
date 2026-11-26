#include "media_workspace.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>

static SemaphoreHandle_t mutex;
static void *arena;
static unsigned frames;
static vibe_media_lease_t active,voice;
static uint32_t generation;

esp_err_t vibe_media_init(void) {
    if (arena) return ESP_OK;
    if (!mutex) mutex=xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    for (unsigned count=V_VOICE_MAX_HISTORY;count>=V_VOICE_TX_SLOTS;count-=2) {
        size_t size=count*sizeof(vibe_voice_tx_slot_t);
        unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DEFAULT|MALLOC_CAP_8BIT;
        if (heap_caps_get_free_size(caps)<size+V_VOICE_HEAP_RESERVE ||
            heap_caps_get_largest_free_block(caps)<size) continue;
        void *candidate=malloc(size);
        /* Wi-Fi/NimBLE can allocate between the sizing check and malloc. */
        if (candidate && heap_caps_get_free_size(caps)>=V_VOICE_HEAP_RESERVE) {
            arena=candidate;
            frames=count;
            ESP_LOGI("vibe_media","Reserved %u blocks, %u bytes for exclusive voice/radio use",frames,(unsigned)size);
            return ESP_OK;
        }
        free(candidate);
    }
    return ESP_ERR_NO_MEM;
}
vibe_media_owner_t vibe_media_owner(void) {
    if (!mutex) return V_MEDIA_IDLE;
    xSemaphoreTake(mutex,portMAX_DELAY);vibe_media_owner_t owner=active.owner;xSemaphoreGive(mutex);
    return owner;
}
bool vibe_media_acquire(vibe_media_owner_t owner,size_t minimum,vibe_media_lease_t *lease) {
    if (!arena || !lease || owner==V_MEDIA_IDLE || owner>V_MEDIA_RADIO || minimum>frames*sizeof(vibe_voice_tx_slot_t)) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool allowed=active.owner==V_MEDIA_IDLE;
    if (allowed) {
        if (!++generation) ++generation;
        active=(vibe_media_lease_t){arena,frames*sizeof(vibe_voice_tx_slot_t),generation,owner};
        *lease=active;
    }
    xSemaphoreGive(mutex);return allowed;
}
void vibe_media_release(vibe_media_lease_t lease) {
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (lease.memory==active.memory && lease.owner==active.owner && lease.generation==active.generation)
        active=(vibe_media_lease_t){0};
    xSemaphoreGive(mutex);
}
vibe_voice_tx_slot_t *vibe_media_voice_acquire(unsigned *capacity) {
    if (!capacity || !vibe_media_acquire(V_MEDIA_VOICE,sizeof(vibe_voice_tx_slot_t)*V_VOICE_TX_SLOTS,&voice)) return NULL;
    *capacity=frames;return voice.memory;
}
void vibe_media_voice_release(vibe_voice_tx_slot_t *slots) {
    if (slots==voice.memory) {vibe_media_release(voice);voice=(vibe_media_lease_t){0};}
}
