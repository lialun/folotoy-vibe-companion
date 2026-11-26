#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Link procedures share the controller with DLE, PHY and encryption. Defer the
 * first parameter request; an accepted command is not a completed negotiation.
 * Timeout units below are the BLE-defined 10 ms units. No permanent timer. */
#define VIBE_BLE_PARAMETER_ATTEMPTS 4
#define VIBE_BLE_MIN_SUPERVISION 200
#define VIBE_BLE_SUPERVISION 600
#define VIBE_BLE_PARAMETER_WAIT_MS 5000
#define VIBE_BLE_STREAM_INTERVAL 12 /* 1.25 ms units: 15 ms. */
#define VIBE_BLE_IDLE_MIN_INTERVAL 12
#define VIBE_BLE_IDLE_MAX_INTERVAL 24
#define VIBE_BLE_IDLE_HOLD_MS 1000

typedef struct {
    uint64_t due,settle_until;
    uint8_t attempts;
    bool waiting,streaming,requested_streaming,relax;
} vibe_ble_parameters_t;
static inline void vibe_ble_parameters_start(vibe_ble_parameters_t *s,uint64_t now) {
    *s=(vibe_ble_parameters_t){.due=now+1000,.settle_until=now+1000};
}
static inline void vibe_ble_parameters_stream(vibe_ble_parameters_t *s,uint64_t now,bool active) {
    if(s->streaming==active)return;
    s->streaming=active;s->relax=!active;s->attempts=0;
    /* An accepted procedure must finish before the newest intent is issued. */
    if(!s->waiting) {
        s->due=now+(active?1:VIBE_BLE_IDLE_HOLD_MS);
        if(s->due<s->settle_until)s->due=s->settle_until;
    }
}
static inline void vibe_ble_parameters_observe(vibe_ble_parameters_t *s,uint64_t now,int status,uint16_t timeout) {
    /* An unsolicited update cannot acknowledge an intent still queued after
     * a busy result. The next due check can adopt its actual parameters. */
    if(!s->waiting && s->due)return;
    s->waiting=false;
    if(s->requested_streaming!=s->streaming) {
        s->attempts=0;s->due=now+(s->streaming?1:VIBE_BLE_IDLE_HOLD_MS);
        if(s->due<s->settle_until)s->due=s->settle_until;
        return;
    }
    /* A valid central-selected fallback is final, even if it kept 30 ms.
     * Do not fight its power policy or reconnect to force the requested rate. */
    if ((!status && timeout>=VIBE_BLE_MIN_SUPERVISION) || s->attempts>=VIBE_BLE_PARAMETER_ATTEMPTS) {
        s->relax=false;s->due=0;return;
    }
    unsigned shift=s->attempts?s->attempts-1:0;
    s->due=now+(1000u<<shift);
}
static inline bool vibe_ble_parameters_due(vibe_ble_parameters_t *s,uint64_t now,uint16_t interval,uint16_t timeout) {
    if (!s->due || now<s->due) return false;
    s->waiting=false;
    if(s->requested_streaming!=s->streaming)s->attempts=0;
    bool suitable=timeout>=VIBE_BLE_MIN_SUPERVISION &&
        (s->streaming ? interval>0 && interval<=VIBE_BLE_STREAM_INTERVAL:
                       !s->relax || interval>=VIBE_BLE_IDLE_MAX_INTERVAL);
    if(suitable){s->relax=false;s->requested_streaming=s->streaming;s->due=0;return false;}
    if (s->attempts>=VIBE_BLE_PARAMETER_ATTEMPTS) {s->due=0;s->waiting=false;return false;}
    ++s->attempts;s->waiting=true;s->requested_streaming=s->streaming;
    s->due=now+VIBE_BLE_PARAMETER_WAIT_MS;return true;
}
