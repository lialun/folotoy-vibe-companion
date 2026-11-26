#pragma once
#include "model.h"
#include <string.h>

typedef enum { V_INPUT_PASS,V_INPUT_CONSUMED,V_INPUT_ACCEPT,V_INPUT_REJECT } vibe_input_result_t;
/* Enter is an explicit, independently identified DOWN press/release command.
 * No voice lifecycle event can synthesize it. Times are captured at the input
 * callback, not reconstructed after a busy application queue drains. */
typedef struct {
    bool up,down,armed,released_up;
    uint64_t up_release,down_at;
    unsigned hold_ms,gap_ms;
    uint32_t command,voice_request,owner;
} vibe_enter_intent_t;
static inline void vibe_enter_intent_reset(vibe_enter_intent_t *s) {
    uint32_t command=s->command;
    *s=(vibe_enter_intent_t){.command=command};
}
static inline bool vibe_enter_intent_event(vibe_enter_intent_t *s,vibe_key_t key,vibe_event_t event,uint64_t now) {
    if (key==V_UP) {
        if(event==V_PRESS){s->up=true;s->armed=false;}
        else if(event==V_RELEASE){s->up=false;s->released_up=true;s->up_release=now;}
    } else if (key==V_DOWN) {
        if(event==V_PRESS) {
            if(s->down){s->armed=false;return false;}
            s->down=true;s->down_at=now;
            if (++s->command==0) ++s->command;
            s->gap_ms=s->released_up && now>=s->up_release ? (unsigned)(now-s->up_release):0;
            s->armed=!s->up;
        } else if(event==V_RELEASE) {
            s->hold_ms=now>=s->down_at?(unsigned)(now-s->down_at):0;
            bool allowed=s->down && s->armed && !s->up && now>s->down_at;
            s->down=false;s->armed=false;return allowed;
        }
    } else if(key==V_SETTINGS && event==V_PRESS) s->armed=false;
    return false;
}
typedef struct {
    char request[37];
    uint32_t frame;
    uint8_t down,consumed,candidate;
    bool active,armed;
} vibe_prompt_input_t;

/* Frame IDs identify a rendered request, not just an intention to light the
 * panel. Press events carry the frame/backlight state at capture time, so a
 * queued wake press cannot approve a page rendered before it is processed. */
static inline void vibe_prompt_input_sync(vibe_prompt_input_t *g,bool active,const char *request,uint32_t frame) {
    if (!active) {
        g->active=false;g->armed=false;g->candidate=0;g->frame=0;g->request[0]=0;return;
    }
    if (!g->active || strcmp(g->request,request)) {
        memcpy(g->request,request,36);g->request[36]=0;
        g->armed=false;g->candidate=0;g->consumed|=g->down;
    }
    g->active=true;g->frame=frame;
    if (!frame) { g->armed=false;g->candidate=0;g->consumed|=g->down; }
    else if (!g->down) g->armed=true;
}
static inline vibe_input_result_t vibe_prompt_input_event(vibe_prompt_input_t *g,vibe_key_t key,
                                                         vibe_event_t event,bool lit,uint32_t captured_frame) {
    if ((unsigned)key>V_SETTINGS) return V_INPUT_CONSUMED;
    uint8_t bit=(uint8_t)(1U<<key);
    if (event==V_PRESS) {
        uint8_t previous=g->down;bool blocked=(g->consumed&bit)!=0;g->down|=bit;
        if (!g->active && !(g->consumed&bit)) return V_INPUT_PASS;
        g->consumed|=bit;
        if (previous || blocked || !g->armed || !lit || !g->frame || captured_frame!=g->frame) g->candidate=0;
        else g->candidate=bit;
        g->armed=false;return V_INPUT_CONSUMED;
    }
    if (event==V_RELEASE) {
        bool captured=(g->consumed&bit)!=0;
        bool decide=g->active && g->frame && lit && captured_frame==g->frame && (g->candidate&bit);
        g->down&=(uint8_t)~bit;g->consumed&=(uint8_t)~bit;g->candidate&=(uint8_t)~bit;
        if (g->active && g->frame && !g->down) g->armed=true;
        if (decide) return key==V_UP?V_INPUT_ACCEPT:V_INPUT_REJECT;
        return captured || g->active?V_INPUT_CONSUMED:V_INPUT_PASS;
    }
    return g->active || (g->consumed&bit)?V_INPUT_CONSUMED:V_INPUT_PASS;
}
