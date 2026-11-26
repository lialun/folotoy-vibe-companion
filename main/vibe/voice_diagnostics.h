#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {
    unsigned failures,free_total,pool;
    int result;
    unsigned partial;
} vibe_ble_voice_pressure_t;
typedef struct {unsigned packets,bytes,completed,failures,gap_ms,processed,host_lag,process_ms,untracked;} vibe_ble_radio_probe_t;
typedef struct {
    unsigned wifi,ble,failed,replaced,expired,repairs;
    vibe_ble_voice_pressure_t pressure;
    vibe_ble_radio_probe_t radio;
    unsigned wifi_oom,wifi_busy,wifi_other;
    int wifi_error;
    unsigned mtu,interval,tx_octets,tx_time,tx_phy,rx_phy;
    unsigned feedback_id,confirmed,retired,history,retention;
} vibe_voice_counters_t;
void vibe_ble_probe_recording(bool active);
vibe_ble_radio_probe_t vibe_ble_radio_probe(void);
/* End-only counters; no speech, credentials or per-packet logging. All numeric
 * fields are optional protocol extensions understood by older clients. */
static inline int vibe_voice_terminal(char *out,size_t capacity,uint64_t session,unsigned frames,
                                     unsigned duration,unsigned gap,unsigned send_gap,unsigned stop,
                                     const vibe_voice_counters_t *before,const vibe_voice_counters_t *after) {
    return snprintf(out,capacity,
        "{\"event\":\"voice.stats\",\"voiceVersion\":2,\"voiceSession\":\"%016llx\",\"frames\":%u,"
        "\"captureMs\":%u,\"captureGapMs\":%u,\"sendGapMs\":%u,\"stop\":%u,"
        "\"tx\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%d],"
        "\"radio\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"net\":[%d,%u,%u,%u],\"feedback\":[%u,%u,%u,%u,%u]}",
        (unsigned long long)session,frames,duration,gap,send_gap,stop,
        after->wifi-before->wifi,after->ble-before->ble,after->failed-before->failed,
        after->replaced-before->replaced,after->expired-before->expired,after->repairs-before->repairs,
        after->pressure.failures-before->pressure.failures,after->pressure.free_total,
        after->pressure.pool,after->pressure.result,
        after->radio.packets-before->radio.packets,after->radio.bytes-before->radio.bytes,
        after->radio.completed-before->radio.completed,after->radio.failures-before->radio.failures,
        after->radio.gap_ms,after->pressure.partial-before->pressure.partial,
        after->mtu,after->interval,after->tx_octets,after->tx_time,after->tx_phy,after->rx_phy,
        after->radio.processed-before->radio.processed,after->radio.host_lag,after->radio.process_ms,after->radio.untracked-before->radio.untracked,
        after->wifi_error,after->wifi_oom-before->wifi_oom,after->wifi_busy-before->wifi_busy,after->wifi_other-before->wifi_other,
        after->feedback_id,after->confirmed,after->retired,after->history,after->retention);
}
