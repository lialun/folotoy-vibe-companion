#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The computer sees return-path loss that incoming beacons cannot reveal. */
static inline bool vibe_peer_use_wifi(uint64_t now,uint64_t wifi_seen,uint64_t ble_seen,
                                     bool has_wifi,bool has_ble,bool prefer_ble) {
    bool wifi_fresh=has_wifi && wifi_seen && now>=wifi_seen && now-wifi_seen<6000;
    bool ble_fresh=has_ble && ble_seen && now>=ble_seen && now-ble_seen<6000;
    return wifi_fresh && !(prefer_ble && ble_fresh);
}

/* Endpoint fields use the socket's network byte order; compare both fields. */
static inline bool vibe_peer_matches(uint32_t address,uint16_t port,uint32_t from_address,uint16_t from_port) {
    return address && port && address==from_address && port==from_port;
}
static inline unsigned vibe_peer_interval(unsigned requested_seconds) {
    return requested_seconds>=5 && requested_seconds<=30 ? requested_seconds*1000 : 5000;
}
static inline unsigned vibe_peer_timeout(unsigned interval_ms) {
    return interval_ms>=5000 && interval_ms<=30000 ? interval_ms*3 : 15000;
}
static inline bool vibe_peer_heartbeat_due(uint64_t now,uint64_t sent,uint64_t seen,bool has_peer,unsigned interval_ms) {
    /* Ordinary traffic replaces fallback pings. Legacy peers retain 5/15 s. */
    return has_peer && seen && now>=seen && now-seen<vibe_peer_timeout(interval_ms) &&
           now>=sent && now-sent>=interval_ms+2000;
}

/* Anonymous discovery never acquires the control endpoint. Claiming a lease
 * requires a trusted credential or a physically approved pairing. */
static inline bool vibe_peer_claim_allowed(bool authenticated,bool pairing,bool occupied,bool same_endpoint) {
    return (authenticated || pairing) && (!occupied || same_endpoint);
}
static inline bool vibe_peer_generation_matches(uint32_t request,uint32_t current) {
    return !request || request==current;
}
