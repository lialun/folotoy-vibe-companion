#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 50 ms, 24 kHz mono, independent IMA ADPCM blocks. No heap in the codec.
 * Segments tolerate a fragmented C3 heap; capacity is fixed for a session. */
enum { V_RELAY_RATE=24000, V_RELAY_SAMPLES=1200, V_RELAY_BYTES=604,
       V_RELAY_SEGMENT_FRAMES=13, V_RELAY_SEGMENTS=3, V_RELAY_HEADER=9,
       V_RELAY_PACKET=V_RELAY_HEADER+V_RELAY_BYTES, V_RELAY_PREFILL=12 };
typedef struct {
    uint8_t *segments[V_RELAY_SEGMENTS];
    unsigned capacity;
    uint32_t stream,received,played;
} vibe_relay_buffer_t;
static inline uint32_t vibe_relay_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline uint8_t *vibe_relay_slot(vibe_relay_buffer_t *b,uint32_t sequence) {
    unsigned index=sequence%b->capacity;
    return b->segments[index/V_RELAY_SEGMENT_FRAMES]+(index%V_RELAY_SEGMENT_FRAMES)*V_RELAY_BYTES;
}
/* Caller serializes access; reject gaps, duplicates, old sessions and overflow.
 * Credit reports received, so a gap is repaired without accumulating a queue. */
static inline bool vibe_relay_push(vibe_relay_buffer_t *b,const uint8_t *p,size_t size) {
    if (!b->capacity || !b->stream || size!=V_RELAY_PACKET || p[0]!=5 ||
        vibe_relay_u32(p+1)!=b->stream || vibe_relay_u32(p+5)!=b->received ||
        b->received-b->played>=b->capacity || p[11]>88 || p[12]!=0) return false;
    for (unsigned i=0;i<V_RELAY_BYTES;i++) vibe_relay_slot(b,b->received)[i]=p[V_RELAY_HEADER+i];
    ++b->received;return true;
}
static inline bool vibe_relay_pop(vibe_relay_buffer_t *b,uint8_t *out) {
    if (!b->capacity || b->received==b->played) return false;
    for (unsigned i=0;i<V_RELAY_BYTES;i++) out[i]=vibe_relay_slot(b,b->played)[i];
    ++b->played;return true;
}
