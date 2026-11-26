#include "voice_tx.h"

_Static_assert(V_VOICE_TX_SLOTS<=VIBE_VOICE_MAX_HISTORY,"Retention must fit the negotiated history range");
_Static_assert(V_VOICE_RETRY_MAX_MS>=V_VOICE_RETRY_MS,"Retry cap must include its initial interval");

/* Local submission cannot shorten a sibling's opportunity. Only receiver
 * feedback can confirm a block or retire no-longer-useful history. */
static unsigned lifetime(const vibe_voice_tx_t *tx) {return tx->retention_ms;}

uint64_t vibe_voice_session(const uint8_t *body) {
    uint64_t value=0;
    for (unsigned i=0;i<8;i++) value|=(uint64_t)body[i]<<(i*8);
    return value;
}

uint32_t vibe_voice_sequence(const uint8_t *body) {
    uint32_t value=0;
    for (unsigned i=0;i<4;i++) value|=(uint32_t)body[8+i]<<(i*8);
    return value;
}

uint64_t vibe_voice_parse_session(const char *text) {
    if (!text || strlen(text)!=16) return 0;
    uint64_t value=0;
    for (unsigned i=0;i<16;i++) {
        unsigned char c=text[i];
        unsigned digit=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : 16;
        if (digit>15) return 0;
        value=(value<<4)|digit;
    }
    return value;
}

void vibe_voice_tx_begin(vibe_voice_tx_t *tx, vibe_voice_tx_slot_t *slots,
                         unsigned capacity, uint64_t session, uint32_t owner) {
    *tx=(vibe_voice_tx_t){.slots=slots,.capacity=capacity,.session=session,
                         .owner=owner,.recording=true,.retention_ms=V_VOICE_RETAIN_MS};
    if (slots) memset(slots,0,capacity*sizeof(*slots));
}

static vibe_voice_tx_slot_t *find(vibe_voice_tx_t *tx, uint64_t session, uint32_t sequence) {
    if (!tx->slots || !tx->capacity || tx->session!=session || sequence>=VIBE_VOICE_MAX_FRAMES) return NULL;
    vibe_voice_tx_slot_t *slot=&tx->slots[sequence%tx->capacity];
    return slot->valid && slot->sequence==sequence ? slot : NULL;
}

bool vibe_voice_tx_enqueue(vibe_voice_tx_t *tx, const void *body, size_t length,
                           uint32_t owner, unsigned links, uint64_t now) {
    if (!body || length!=VIBE_VOICE_BODY || !tx->recording || tx->cancelled || !tx->capacity ||
        tx->owner!=owner || vibe_voice_session(body)!=tx->session) return false;
    uint32_t sequence=vibe_voice_sequence(body);
    if (sequence>=VIBE_VOICE_MAX_FRAMES || sequence<tx->produced) return false;
    vibe_voice_tx_slot_t *slot=&tx->slots[sequence%tx->capacity];
    if (slot->valid && slot->sequence==sequence) return false;
    if (slot->valid && slot->pending) ++tx->overwritten;
    *slot=(vibe_voice_tx_slot_t){.created=now,.sequence=sequence,.pending=links&3,.valid=true};
    memcpy(slot->bytes,body,VIBE_VOICE_BODY);
    ++tx->queued;
    tx->receiver_done=false;
    if (sequence>=tx->produced) tx->produced=sequence+1;
    return true;
}

bool vibe_voice_tx_repair(vibe_voice_tx_t *tx, uint64_t session, uint32_t sequence,
                          unsigned links, uint64_t now,uint32_t request_id) {
    vibe_voice_tx_slot_t *slot=find(tx,session,sequence);
    if (!slot || slot->received || tx->cancelled || now<slot->created || now-slot->created>lifetime(tx) ||
        slot->requests>=V_VOICE_MAX_REPAIRS || (!request_id && slot->repair) || !(links&3) ||
        (request_id && request_id<=slot->last_repair_id)) return false;
    unsigned scheduled=(links&3)&~slot->repair;
    if (!scheduled) return false;
    ++slot->requests;
    if (request_id) slot->last_repair_id=request_id;
    /* A blocked mirror cannot hold the other carrier's next logical attempt.
     * Keep existing repairs/cursors intact and rearm only newly scheduled legs. */
    slot->repair|=scheduled;
    slot->pending|=scheduled;
    for (unsigned leg=0;leg<2;leg++) if (scheduled&(1u<<leg)) {
        slot->tries[leg]=0;
    }
    return true;
}

bool vibe_voice_tx_take(vibe_voice_tx_t *tx, unsigned leg, uint64_t now,
                        uint8_t body[VIBE_VOICE_BODY], vibe_voice_tx_ticket_t *ticket) {
    if (leg>1 || tx->cancelled) return false;
    unsigned bit=1u<<leg;
    vibe_voice_tx_slot_t *best=NULL,*continuing=NULL;
    unsigned best_remaining=0;
    for (unsigned i=0;i<tx->capacity;i++) {
        vibe_voice_tx_slot_t *slot=&tx->slots[i];
        if (!slot->valid || slot->received || !(slot->pending&bit)) continue;
        unsigned ttl=lifetime(tx);
        if (now<slot->created || now-slot->created>ttl || slot->tries[leg]>=V_VOICE_MAX_TRIES) {
            slot->pending&=~bit;
            slot->repair&=~bit;
            ++tx->expired;
            continue;
        }
        if ((tx->leased&bit) && slot->sequence==tx->lease[leg]) continuing=slot;
        /* Resource pressure belongs to the carrier, not one frame. New frames
         * and NACKs must not bypass a congested carrier's recovery interval. */
        if (now<tx->retry_at[leg]) continue;
        unsigned remaining=ttl-(unsigned)(now-slot->created);
        /* Earliest expiry first keeps old repairs from starving behind a
         * continuous stream of fresh capture. Ties preserve sequence order. */
        if (!best || remaining<best_remaining ||
            (remaining==best_remaining && slot->sequence<best->sequence)) {
            best=slot;best_remaining=remaining;
        }
    }
    /* The peer owns one assembly per carrier. Once a prefix is submitted,
     * EDF cannot replace it with another frame and silently discard that
     * prefix. A lease still obeys backoff, expiry, eviction and attempt caps. */
    if (!continuing) tx->leased&=~bit;
    else if (now>=tx->retry_at[leg]) best=continuing;
    if (!best) return false;
    *ticket=(vibe_voice_tx_ticket_t){.session=tx->session,.created=best->created,
        .owner=tx->owner,.sequence=best->sequence,.leg=leg,.repair=(best->repair&bit)!=0};
    best->pending&=~bit;
    ++best->tries[leg];
    memcpy(body,best->bytes,VIBE_VOICE_BODY);
    return true;
}

void vibe_voice_tx_complete(vibe_voice_tx_t *tx, const vibe_voice_tx_ticket_t *ticket,
                            uint64_t now, vibe_voice_submission_t submission) {
    if (ticket->leg>1 || ticket->owner!=tx->owner || tx->cancelled) return;
    vibe_voice_tx_slot_t *slot=find(tx,ticket->session,ticket->sequence);
    if (!slot || slot->received || slot->created!=ticket->created || now<slot->created) return;
    unsigned bit=1u<<ticket->leg;
    if (submission==V_VOICE_SUBMITTED) {
        tx->leased&=~bit;
        tx->failures[ticket->leg]=0;tx->retry_at[ticket->leg]=now;
        ++tx->sent[ticket->leg];
        if (ticket->repair) ++tx->repaired;
        slot->repair&=~bit;
    } else if (submission==V_VOICE_PARTIAL) {
        /* Forward progress is normal credit flow, not a failed transmission.
         * The outer service loop still caps attempts to one per 20 ms/leg. */
        tx->failures[ticket->leg]=0;tx->retry_at[ticket->leg]=now;
        if (now-slot->created>lifetime(tx)) {
            tx->leased&=~bit;slot->repair&=~bit;++tx->expired;return;
        }
        slot->pending|=bit;slot->tries[ticket->leg]=0;
        tx->lease[ticket->leg]=slot->sequence;tx->leased|=bit;
    } else {
        ++tx->failed;
        unsigned delay=V_VOICE_RETRY_MS;
        for (unsigned i=0;i<tx->failures[ticket->leg] && delay<V_VOICE_RETRY_MAX_MS;i++)
            delay=delay<=V_VOICE_RETRY_MAX_MS/2 ? delay*2:V_VOICE_RETRY_MAX_MS;
        if (delay<V_VOICE_RETRY_MAX_MS) ++tx->failures[ticket->leg];
        tx->retry_at[ticket->leg]=now+delay;
        unsigned ttl=lifetime(tx);
        if (now-slot->created<=ttl && slot->tries[ticket->leg]<V_VOICE_MAX_TRIES) {
            slot->pending|=bit;
        } else {
            tx->leased&=~bit;
            slot->repair&=~bit;
            ++tx->expired;
        }
    }
}

void vibe_voice_tx_end(vibe_voice_tx_t *tx, uint64_t session, uint32_t owner, uint64_t now) {
    if (tx->session!=session || tx->owner!=owner) return;
    tx->recording=false;
    tx->release_at=now+lifetime(tx);
}

bool vibe_voice_tx_live(const vibe_voice_tx_t *tx, uint32_t owner, uint64_t now) {
    if (!tx->slots || tx->owner!=owner || tx->cancelled) return false;
    if (tx->recording) return true;
    if (tx->feedback && tx->receiver_done) return false;
    return now<tx->release_at;
}

bool vibe_voice_tx_pending(const vibe_voice_tx_t *tx) {
    for (unsigned i=0;i<tx->capacity;i++) if (tx->slots[i].pending) return true;
    return false;
}

bool vibe_voice_tx_feedback(vibe_voice_tx_t *tx,const vibe_voice_feedback_t *f,uint32_t owner) {
    if (!f || !tx->feedback || tx->cancelled || tx->owner!=owner || tx->session!=f->session ||
        !f->id || f->id<=tx->feedback_id || f->retire_before<tx->retire_before || f->retire_before>tx->produced) return false;
    if (f->finished && (f->retire_before!=tx->produced || f->received)) return false;
    for (unsigned i=0;i<V_VOICE_FEEDBACK_WINDOW;i++)
        if ((f->received&(UINT32_C(1)<<i)) && (uint64_t)f->retire_before+i>=tx->produced) return false;
    tx->feedback_id=f->id;tx->retire_before=f->retire_before;
    tx->receiver_done=tx->receiver_done || f->finished;
    for (unsigned i=0;i<tx->capacity;i++) {
        vibe_voice_tx_slot_t *slot=&tx->slots[i];
        if (!slot->valid) continue;
        bool retire=slot->sequence<f->retire_before;
        uint32_t offset=slot->sequence-f->retire_before;
        bool received=!retire && offset<V_VOICE_FEEDBACK_WINDOW && (f->received&(UINT32_C(1)<<offset));
        if (!retire && !received) continue;
        if (received && !slot->received) ++tx->confirmed;
        if (retire && !slot->received) ++tx->retired;
        slot->pending=slot->repair=0;
        for (unsigned leg=0;leg<2;leg++) if ((tx->leased&(1u<<leg)) && tx->lease[leg]==slot->sequence) tx->leased&=~(1u<<leg);
        if (retire) slot->valid=false;else slot->received=true;
    }
    return true;
}
