#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "voice_policy.h"

/* One bounded notification stream; payload remains in the uplink ring. */
typedef struct {
    uint64_t session,started;
    uint32_t sequence,generation;
    uint16_t handle;
    unsigned char type,message;
    size_t total,offset;
    bool active;
} vibe_ble_voice_cursor_t;

static inline bool vibe_ble_voice_prepare(vibe_ble_voice_cursor_t *cursor,
        uint16_t handle,uint32_t generation,uint64_t session,uint32_t sequence,
        unsigned char type,size_t total,uint64_t now) {
    if (cursor->active && cursor->handle==handle && cursor->generation==generation &&
        cursor->session==session && cursor->sequence==sequence && cursor->type==type &&
        cursor->total==total && now>=cursor->started && now-cursor->started<=V_VOICE_RETAIN_MS) return false;
    *cursor=(vibe_ble_voice_cursor_t){.handle=handle,.generation=generation,
        .session=session,.sequence=sequence,.type=type,.total=total,.started=now,.active=true};
    return true;
}
