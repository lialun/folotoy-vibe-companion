#pragma once
#include "voice_wire.h"
#include <stdbool.h>
/* One pending copy, not a backlog. An in-flight send has its own static slot.
 * Call under the network's copy mutex. No heap allocation or RTOS dependency. */
typedef struct {
    uint8_t bytes[VIBE_VOICE_BODY];
    uint64_t born;
    uint32_t owner,link;
    uint16_t handle;
    bool valid;
} vibe_voice_copy_t;
static inline bool vibe_voice_copy_offer(vibe_voice_copy_t *slot,const void *bytes,size_t n,
                                         uint64_t now,uint32_t owner,uint16_t handle,uint32_t link) {
    if (!bytes || n!=sizeof(slot->bytes) || !owner || !link) return false;
    bool replaced=slot->valid;
    memcpy(slot->bytes,bytes,n);slot->born=now;slot->owner=owner;slot->handle=handle;slot->link=link;slot->valid=true;
    return replaced;
}
static inline bool vibe_voice_copy_take(vibe_voice_copy_t *slot,vibe_voice_copy_t *out,
                                        uint64_t now,uint32_t owner) {
    if (!slot->valid) return false;
    slot->valid=false;
    if (slot->owner!=owner || now<slot->born || now-slot->born>200) return false;
    *out=*slot;return true;
}
/* A failed in-flight copy may retry without extending its original lifetime.
 * Never overwrite a newer pending frame with that older retry. */
static inline bool vibe_voice_copy_retry(vibe_voice_copy_t *slot,const vibe_voice_copy_t *copy,
                                         uint64_t now,uint32_t owner) {
    if (slot->valid || !owner || copy->owner!=owner || !copy->link ||
        now<copy->born || now-copy->born>200) return false;
    *slot=*copy;slot->valid=true;return true;
}
