#pragma once
#include "esp_err.h"
#include "voice_tx.h"

/* One reserved arena, borrowed by exactly one media lifetime. No allocation in
 * capture/send loops. Radio returns its lease before voice can start. */
typedef enum { V_MEDIA_IDLE, V_MEDIA_VOICE, V_MEDIA_RADIO } vibe_media_owner_t;
typedef struct {
    void *memory;
    size_t bytes;
    uint32_t generation;
    vibe_media_owner_t owner;
} vibe_media_lease_t;

esp_err_t vibe_media_init(void);
vibe_media_owner_t vibe_media_owner(void);
bool vibe_media_acquire(vibe_media_owner_t owner,size_t minimum,vibe_media_lease_t *lease);
void vibe_media_release(vibe_media_lease_t lease);
/* Uplink storage ports; serialized by its existing ring mutex. */
vibe_voice_tx_slot_t *vibe_media_voice_acquire(unsigned *capacity);
void vibe_media_voice_release(vibe_voice_tx_slot_t *slots);
