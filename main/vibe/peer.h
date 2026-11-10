#pragma once
#include <stdbool.h>
#include <stdint.h>

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
