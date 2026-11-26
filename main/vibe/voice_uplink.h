#pragma once

#include "esp_err.h"
#include "voice_tx.h"
#include "voice_diagnostics.h"

/* The connection owner supplies authenticated endpoints. Only service(), called
 * by its network worker, invokes send. Capture only enqueues encoded blocks. */
typedef struct {
    uint32_t (*owner)(void);
    unsigned (*links)(uint32_t owner,vibe_voice_mode_t mode);
    vibe_voice_submission_t (*send)(vibe_voice_mode_t mode,const vibe_voice_tx_ticket_t *ticket,
                const uint8_t *body,size_t length,int *network_error);
    void (*wake)(void);
    /* Optional platform effect, invoked by service outside the ring mutex. */
    void (*activity)(bool active);
    vibe_voice_tx_slot_t *(*acquire)(unsigned *capacity);
    void (*release)(vibe_voice_tx_slot_t *slots);
} vibe_voice_uplink_ports_t;

int vibe_send_voice(const void *data,size_t len,uint32_t owner);
unsigned vibe_voice_begin(uint64_t session,uint32_t owner,vibe_voice_mode_t mode,bool feedback);
unsigned vibe_voice_links(uint32_t owner);
void vibe_voice_end(uint64_t session,uint32_t owner);
void vibe_voice_report(void);
vibe_voice_counters_t vibe_voice_counters(void);

esp_err_t vibe_voice_uplink_init(vibe_voice_uplink_ports_t ports);
/* Maintenance and activity cleanup always run. Start-before-audio ordering
 * may suppress media submission without retaining a stale power assertion. */
void vibe_voice_uplink_service(bool may_transmit);
/* Only an acknowledged receiver can safely accept audio ahead of queued
 * control. Older peers require start-before-audio ordering. */
bool vibe_voice_uplink_can_overtake_control(void);
unsigned vibe_voice_uplink_wait(unsigned maximum_ms);
bool vibe_voice_repair_request(uint64_t session, const uint32_t *frames,
                               unsigned count, uint32_t owner,uint32_t request_id);
void vibe_voice_uplink_ready(uint64_t session, uint32_t owner);
bool vibe_voice_uplink_is_ready(uint64_t session, uint32_t owner);
void vibe_voice_uplink_cancel(uint64_t session, uint32_t owner);
bool vibe_voice_uplink_cancelled(uint64_t session, uint32_t owner);

bool vibe_voice_uplink_feedback(const vibe_voice_feedback_t *feedback,uint32_t owner);
