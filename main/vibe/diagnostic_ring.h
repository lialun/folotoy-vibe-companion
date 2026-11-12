#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define V_DIAGNOSTIC_CAPACITY 8
enum {
    V_DIAG_BOOT=1,
    V_DIAG_RADIO_MEMORY=101,V_DIAG_RADIO_REMOTE=102,V_DIAG_RADIO_TIMEOUT=103,
    V_DIAG_RADIO_DECODE=104,V_DIAG_RADIO_OUTPUT=105,
    V_DIAG_AUDIO_PREPARE=201,V_DIAG_AUDIO_CAPTURE=202,V_DIAG_AUDIO_SLEEP=203,
    V_DIAG_INPUT_OVERFLOW=301,V_DIAG_SETTINGS_SAVE=401
};
typedef struct {
    uint32_t sequence,uptime_ms,free,largest;
    int32_t detail;
    uint16_t code;
    uint8_t link,reserved;
} vibe_diagnostic_t;
typedef struct { vibe_diagnostic_t rows[V_DIAGNOSTIC_CAPACITY];uint32_t sequence,count; } vibe_diagnostic_ring_t;
_Static_assert(sizeof(vibe_diagnostic_t)==24,"Keep diagnostic entries fixed and small");
_Static_assert(sizeof(vibe_diagnostic_ring_t)==200,"Keep the device diagnostic ring bounded");
static inline void vibe_diagnostic_push(vibe_diagnostic_ring_t *r,vibe_diagnostic_t row) {
    /* Sequence zero is reserved for an empty boot. Wrap starts a fresh window. */
    if (r->sequence==UINT32_MAX) memset(r,0,sizeof(*r));
    row.sequence=++r->sequence;r->rows[(row.sequence-1)%V_DIAGNOSTIC_CAPACITY]=row;
    if (r->count<V_DIAGNOSTIC_CAPACITY) ++r->count;
}
static inline unsigned vibe_diagnostic_read(const vibe_diagnostic_ring_t *r,uint32_t after,
                                          vibe_diagnostic_t *out,unsigned capacity) {
    if (!r->count || !capacity) return 0;
    uint32_t first=r->sequence-r->count+1;
    if (after>=first && after<r->sequence) first=after+1;
    else if (after>=r->sequence) return 0;
    unsigned n=0;
    for (uint32_t i=first;n<capacity;++i) {
        out[n++]=r->rows[(i-1)%V_DIAGNOSTIC_CAPACITY];
        if (i==r->sequence) break;
    }
    return n;
}
