#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Notification success means queued, not delivered. Leave space for incoming
 * writes and their ATT responses instead of treating NOTIFY_TX as radio credit. */
static inline bool vibe_ble_tx_admit(unsigned total_free,unsigned pool_free,bool audio) {
    return total_free>=(audio?6u:2u) && pool_free>=(audio?4u:1u);
}
static inline bool vibe_ble_tx_frame_admit(unsigned total_free,unsigned pool_free,bool audio,
                                          bool first,size_t total,unsigned fragment) {
    unsigned extra=0;
    if (audio && first && fragment) {
        unsigned count=(unsigned)((total+fragment-1)/fragment);
        /* Reserve whole-frame capacity globally. Consecutive fragments may
         * use different pools; demanding the whole frame from the selected
         * pool rejects usable aggregate capacity. Each actual allocation must
         * still leave that pool's normal control reserve intact. Small-MTU
         * peers use streaming admission because one frame exceeds a pool. */
        if (count<=8) extra=count?count-1:0;
    }
    return total_free>=extra &&
        vibe_ble_tx_admit(total_free-extra,pool_free,audio);
}
static inline bool vibe_ble_tx_wait(uint64_t began,uint64_t now,bool audio) {
    return now>=began && now-began<(audio?30u:50u);
}
static inline unsigned vibe_ble_tx_lock_ms(bool audio) {return audio?30u:50u;}
