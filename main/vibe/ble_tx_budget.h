#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Notification success means queued, not delivered. Leave space for incoming
 * writes and their ATT responses instead of treating NOTIFY_TX as radio credit. */
static inline bool vibe_ble_tx_admit(unsigned total_free,unsigned pool_free,bool audio) {
    return total_free>=(audio?6u:2u) && pool_free>=(audio?4u:1u);
}
static inline bool vibe_ble_tx_wait(uint64_t began,uint64_t now,bool audio) {
    return now>=began && now-began<(audio?30u:50u);
}
