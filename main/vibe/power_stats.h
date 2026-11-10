#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Totals only: no history, timer, task or per-wakeup allocation. */
typedef struct {
    uint64_t started_us, sleep_us, longest_us;
    uint64_t blank_since_us, blank_us, blank_sleep_us;
    uint32_t sleeps, rejects;
    bool blank;
} vibe_power_stats_t;

static inline void vibe_power_stats_screen(vibe_power_stats_t *s, bool awake, uint64_t now) {
    if (s->blank == !awake) return;
    if (s->blank && now >= s->blank_since_us) s->blank_us += now-s->blank_since_us;
    s->blank = !awake;
    s->blank_since_us = now;
}
static inline void vibe_power_stats_sleep(vibe_power_stats_t *s, bool success, uint64_t duration) {
    if (!success) { ++s->rejects; return; }
    ++s->sleeps;
    s->sleep_us += duration;
    if (s->blank) s->blank_sleep_us += duration;
    if (duration > s->longest_us) s->longest_us = duration;
}
