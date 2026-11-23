#pragma once
#include "adpcm.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define VIBE_VOICE_HEADER 12
#define VIBE_VOICE_BODY (VIBE_VOICE_HEADER+ADPCM_BLOCK_BYTES)
#define VIBE_VOICE_TYPE 6
#define VIBE_VOICE_MAX_FRAMES 2000u
/* Body of type 6: nonzero LE session u64, LE block sequence u32, one independent
 * 100 ms IMA block. Ownership is still checked on the authenticated attachment. */
static inline size_t vibe_voice_encode(uint64_t session,uint32_t sequence,const void *block,size_t n,uint8_t *out,size_t capacity) {
    if (!session || sequence>=VIBE_VOICE_MAX_FRAMES || !block || !out || n!=ADPCM_BLOCK_BYTES || capacity<VIBE_VOICE_BODY) return 0;
    const uint8_t *b=block;if (b[2]>88 || b[3]) return 0;
    memmove(out+VIBE_VOICE_HEADER,block,n);
    for(unsigned i=0;i<8;i++) out[i]=(uint8_t)(session>>(i*8));
    for(unsigned i=0;i<4;i++) out[8+i]=(uint8_t)(sequence>>(i*8));
    return VIBE_VOICE_BODY;
}
