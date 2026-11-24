#pragma once
#include "voice_wire.h"
#include <stdbool.h>

#define VIBE_VOICE_HISTORY_MAX 4u
#define VIBE_VOICE_REPAIR_AGE (VIBE_VOICE_HISTORY_MAX*100u)
typedef enum { V_VOICE_WIFI_RETRY=1,V_VOICE_REPAIR=2 } vibe_voice_attempt_kind_t;
typedef struct {
    uint8_t bytes[VIBE_VOICE_BODY];
    uint64_t born,requested_at,wifi_due,repair_due;
    uint32_t sequence;
    unsigned char wifi_tries,repair_tries,requests;
    bool valid,wifi_pending,repair_pending;
} vibe_voice_history_slot_t;
typedef struct {
    vibe_voice_history_slot_t *slots;
    unsigned count;
    uint64_t session,release_at;
    uint32_t owner;
    bool recording;
} vibe_voice_history_t;
typedef struct {
    uint64_t session,born;
    uint32_t owner,sequence;
    vibe_voice_attempt_kind_t kind;
} vibe_voice_attempt_t;
static inline uint64_t vibe_voice_session(const uint8_t *body) {
    uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)body[i]<<(i*8);return value;
}
static inline uint32_t vibe_voice_sequence(const uint8_t *body) {
    uint32_t value=0;for(unsigned i=0;i<4;i++)value|=(uint32_t)body[8+i]<<(i*8);return value;
}
static inline uint64_t vibe_voice_parse_session(const char *text) {
    if(!text || strlen(text)!=16)return 0;
    uint64_t value=0;
    for(unsigned i=0;i<16;i++) {
        unsigned char c=text[i];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;
        if(d>15)return 0;
        value=(value<<4)|d;
    }
    return value;
}
static inline void vibe_voice_history_init(vibe_voice_history_t *h,vibe_voice_history_slot_t *slots,
                                           unsigned count,uint64_t session,uint32_t owner) {
    *h=(vibe_voice_history_t){.slots=slots,.count=count,.session=session,.owner=owner,.recording=true};
    memset(slots,0,count*sizeof(*slots));
}
static inline vibe_voice_history_slot_t *vibe_voice_history_find(vibe_voice_history_t *h,uint64_t session,uint32_t seq) {
    if(!h->slots || !h->count || session!=h->session || seq>=VIBE_VOICE_MAX_FRAMES)return NULL;
    vibe_voice_history_slot_t *s=&h->slots[seq%h->count];
    return s->valid && s->sequence==seq?s:NULL;
}
static inline bool vibe_voice_history_store(vibe_voice_history_t *h,const uint8_t *body,uint32_t owner,uint64_t now) {
    if(!h->slots || !h->recording || owner!=h->owner || vibe_voice_session(body)!=h->session)return false;
    uint32_t seq=vibe_voice_sequence(body);if(seq>=VIBE_VOICE_MAX_FRAMES)return false;
    vibe_voice_history_slot_t *s=&h->slots[seq%h->count];
    *s=(vibe_voice_history_slot_t){.born=now,.sequence=seq,.valid=true};memcpy(s->bytes,body,VIBE_VOICE_BODY);return true;
}
static inline bool vibe_voice_history_request(vibe_voice_history_t *h,uint64_t session,uint32_t seq,uint64_t now) {
    vibe_voice_history_slot_t *s=vibe_voice_history_find(h,session,seq);
    if(!s || now<s->born || now-s->born>VIBE_VOICE_REPAIR_AGE || s->requests>=2 || s->repair_pending ||
       (s->requests && (now<s->requested_at || now-s->requested_at<60)))return false;
    ++s->requests;s->requested_at=now;s->repair_tries=0;s->repair_pending=true;s->repair_due=now;return true;
}
static inline bool vibe_voice_history_retry_wifi(vibe_voice_history_t *h,uint64_t session,uint32_t seq,uint64_t now) {
    vibe_voice_history_slot_t *s=vibe_voice_history_find(h,session,seq);
    if(!s || now<s->born || now-s->born>200)return false;
    s->wifi_pending=true;s->wifi_due=now+10;return true;
}
static inline bool vibe_voice_history_take(vibe_voice_history_t *h,uint64_t now,uint8_t *body,vibe_voice_attempt_t *out) {
    vibe_voice_history_slot_t *best=NULL;vibe_voice_attempt_kind_t kind=0;
    for(unsigned i=0;i<h->count;i++) {
        vibe_voice_history_slot_t *s=&h->slots[i];if(!s->valid)continue;
        if(now<s->born || now-s->born>200)s->wifi_pending=false;
        if(now<s->born || now-s->born>VIBE_VOICE_REPAIR_AGE)s->repair_pending=false;
        vibe_voice_attempt_kind_t k=s->repair_pending&&now>=s->repair_due?V_VOICE_REPAIR:
            s->wifi_pending&&now>=s->wifi_due?V_VOICE_WIFI_RETRY:0;
        if(k && (!best || s->sequence<best->sequence)){best=s;kind=k;}
    }
    if(!best)return false;
    *out=(vibe_voice_attempt_t){.session=h->session,.owner=h->owner,.sequence=best->sequence,.born=best->born,.kind=kind};
    if(kind==V_VOICE_REPAIR){best->repair_pending=false;++best->repair_tries;}
    else {best->wifi_pending=false;++best->wifi_tries;}
    memcpy(body,best->bytes,VIBE_VOICE_BODY);return true;
}
static inline void vibe_voice_history_complete(vibe_voice_history_t *h,const vibe_voice_attempt_t *a,uint64_t now,bool success) {
    vibe_voice_history_slot_t *s=vibe_voice_history_find(h,a->session,a->sequence);
    if(!s || a->owner!=h->owner || s->born!=a->born || now<s->born)return;
    if(a->kind==V_VOICE_REPAIR) {
        if(success)s->wifi_pending=false;
        else if(now-s->born<=VIBE_VOICE_REPAIR_AGE && s->repair_tries<3){s->repair_pending=true;s->repair_due=now+20;}
    } else if(!success && now-s->born<=200 && s->wifi_tries<8){s->wifi_pending=true;s->wifi_due=now+20;}
}
static inline bool vibe_voice_history_pending(const vibe_voice_history_t *h) {
    for(unsigned i=0;i<h->count;i++)if(h->slots[i].wifi_pending || h->slots[i].repair_pending)return true;
    return false;
}
