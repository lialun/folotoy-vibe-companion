#pragma once
#include "wifi_profiles.h"
#include <string.h>

typedef struct {
    bool pending,round_done;
    unsigned attempt,round;
    uint64_t due,started;
} vibe_wifi_retry_t;
static inline void vibe_wifi_retry_reset(vibe_wifi_retry_t *r,uint64_t now) {
    *r=(vibe_wifi_retry_t){.due=now};
}
static inline void vibe_wifi_retry_failed(vibe_wifi_retry_t *r,uint64_t now,unsigned count) {
    r->pending=false;
    if (++r->attempt>=count) {
        r->attempt=0;r->round_done=true;
        r->due=now+vibe_wifi_retry_delay_ms(r->round);
        if (r->round<4) r->round++;
    } else r->due=now+500;
}
static inline bool vibe_wifi_retry_timeout(const vibe_wifi_retry_t *r,uint64_t now) {
    return r->pending && now>=r->started && now-r->started>=15000;
}
static inline bool vibe_wifi_retry_due(const vibe_wifi_retry_t *r,uint64_t now,unsigned count) {
    return count && !r->pending && now>=r->due;
}
static inline void vibe_wifi_retry_started(vibe_wifi_retry_t *r,uint64_t now) {
    r->pending=true;r->started=now;
}
/* Delayed events from the previous SSID cannot invalidate a newer link. */
static inline bool vibe_wifi_disconnect_relevant(const char *target,const uint8_t *ssid,
                                                size_t length,bool associated) {
    if (associated) return false;
    if (!ssid || !length) return true; /* Driver did not identify the failed AP. */
    return length<=32 && strlen(target)==length && !memcmp(target,ssid,length);
}
static inline bool vibe_wifi_offer_setup(const vibe_wifi_retry_t *r,uint64_t now,uint64_t boot,
                                        bool has_saved,bool associated) {
    return !associated && !r->pending &&
        (!has_saved || (r->round_done && now>=boot && now-boot>20000));
}
