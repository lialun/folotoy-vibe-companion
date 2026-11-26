#pragma once

#include "voice_wire.h"
#include "voice_policy.h"
#include "voice_feedback.h"
#include <stdbool.h>

enum { V_VOICE_WIFI=1, V_VOICE_BLE=2 };
/* PARTIAL means new bytes advanced, not merely that an old prefix exists. */
typedef enum {
    V_VOICE_NOT_SUBMITTED=-1, V_VOICE_SUBMITTED=0, V_VOICE_PARTIAL=1
} vibe_voice_submission_t;

typedef struct {
    uint8_t bytes[VIBE_VOICE_BODY];
    uint64_t created;
    uint32_t sequence,last_repair_id;
    uint8_t pending, repair, tries[2], requests;
    bool valid,received;
} vibe_voice_tx_slot_t;

typedef struct {
    vibe_voice_tx_slot_t *slots;
    unsigned capacity;
    uint64_t session, release_at;
    uint32_t owner;
    uint64_t retry_at[2];
    uint32_t lease[2];
    unsigned leased;
    uint8_t failures[2];
    bool recording, ready, cancelled;
    bool feedback,receiver_done;
    unsigned retention_ms;
    uint32_t produced,feedback_id,retire_before;
    unsigned confirmed,retired;
    unsigned queued, overwritten, expired, repaired, failed;
    unsigned sent[2];
} vibe_voice_tx_t;

/* A send owns a copy, never a pointer into the capture ring. A completion from
 * an overwritten slot or previous owner cannot mutate a newer recording. */
typedef struct {
    uint64_t session, created;
    uint32_t owner, sequence;
    unsigned leg;
    bool repair;
} vibe_voice_tx_ticket_t;

uint64_t vibe_voice_session(const uint8_t *body);
uint32_t vibe_voice_sequence(const uint8_t *body);
uint64_t vibe_voice_parse_session(const char *text);
void vibe_voice_tx_begin(vibe_voice_tx_t *tx, vibe_voice_tx_slot_t *slots,
                         unsigned capacity, uint64_t session, uint32_t owner);
bool vibe_voice_tx_enqueue(vibe_voice_tx_t *tx, const void *body, size_t length,
                           uint32_t owner, unsigned links, uint64_t now);
/* Nonzero request IDs increase within one recording; a mirrored/stale ID
 * never charges the request budget twice. Zero preserves legacy behavior. */
bool vibe_voice_tx_repair(vibe_voice_tx_t *tx, uint64_t session, uint32_t sequence,
                          unsigned links, uint64_t now,uint32_t request_id);
bool vibe_voice_tx_take(vibe_voice_tx_t *tx, unsigned leg, uint64_t now,
                        uint8_t body[VIBE_VOICE_BODY], vibe_voice_tx_ticket_t *ticket);
void vibe_voice_tx_complete(vibe_voice_tx_t *tx, const vibe_voice_tx_ticket_t *ticket,
                            uint64_t now, vibe_voice_submission_t submission);
void vibe_voice_tx_end(vibe_voice_tx_t *tx, uint64_t session, uint32_t owner, uint64_t now);
bool vibe_voice_tx_live(const vibe_voice_tx_t *tx, uint32_t owner, uint64_t now);
bool vibe_voice_tx_pending(const vibe_voice_tx_t *tx);
bool vibe_voice_tx_feedback(vibe_voice_tx_t *tx,const vibe_voice_feedback_t *feedback,uint32_t owner);
