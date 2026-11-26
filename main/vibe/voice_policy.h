#pragma once

/* Local resource/scheduling policy, not wire-format fields. Keep time budgets
 * here so a retry or memory change cannot silently diverge across workers. */
enum {
    V_VOICE_TX_SLOTS=6,
    V_VOICE_RETAIN_MS=600,
    V_VOICE_HISTORY_MS=3000,
    V_VOICE_MAX_HISTORY=30,
    V_VOICE_RETRY_MS=20,
    V_VOICE_RETRY_MAX_MS=80,
    V_VOICE_MAX_TRIES=8,
    V_VOICE_MAX_REPAIRS=2,
    V_VOICE_HEAP_RESERVE=22*1024,
    V_VOICE_READY_MS=1500,
    V_VOICE_START_RETRY_MS=250,
    V_VOICE_RECORD_MS=180000,
};
