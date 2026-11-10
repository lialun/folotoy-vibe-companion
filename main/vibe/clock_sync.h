#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Owned only by the existing network task; deadline is an attempt timeout or
 * retry time. No task, timer, queue or persisted timestamp belongs to this state. */
typedef struct {
    uint64_t deadline;
    uint8_t failures,server;
    bool active;
} vibe_clock_sync_t;
typedef enum { V_CLOCK_NONE,V_CLOCK_START,V_CLOCK_STOP } vibe_clock_action_t;
static inline bool vibe_clock_valid(int64_t epoch) {
    return epoch>=1700000000LL && epoch<=4102444800LL;
}
static inline void vibe_clock_failed(vibe_clock_sync_t *s,uint64_t now) {
    static const unsigned delays[]={5000,60000,300000,900000};
    if (s->failures<4) ++s->failures;
    s->deadline=now+delays[s->failures-1];
    s->server=(uint8_t)((s->server+1)%3);s->active=false;
}
static inline vibe_clock_action_t vibe_clock_step(vibe_clock_sync_t *s,uint64_t now,
                                                 bool wifi,bool valid,bool allowed) {
    if (valid) {
        bool stop=s->active;*s=(vibe_clock_sync_t){0};
        return stop?V_CLOCK_STOP:V_CLOCK_NONE;
    }
    if (!wifi) {
        bool stop=s->active;s->active=false;s->deadline=0;
        return stop?V_CLOCK_STOP:V_CLOCK_NONE;
    }
    if (s->active) {
        if (!allowed) { s->active=false;s->deadline=now+5000;return V_CLOCK_STOP; }
        if (now>=s->deadline) { vibe_clock_failed(s,now);return V_CLOCK_STOP; }
        return V_CLOCK_NONE;
    }
    if (now<s->deadline) return V_CLOCK_NONE;
    if (!allowed) { s->deadline=now+30000;return V_CLOCK_NONE; }
    s->active=true;s->deadline=now+12000;return V_CLOCK_START;
}
static inline unsigned vibe_clock_wait(const vibe_clock_sync_t *s,uint64_t now,unsigned wait) {
    if (!s->deadline) return wait;
    if (s->active && wait>1000) wait=1000; /* Briefly poll completion, then stop. */
    uint64_t left=s->deadline>now?s->deadline-now:1;
    return left<wait?(unsigned)left:wait;
}
void vibe_clock_poll(vibe_clock_sync_t *state,uint64_t now,bool wifi,bool paused);
