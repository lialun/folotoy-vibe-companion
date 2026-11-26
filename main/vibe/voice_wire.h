#pragma once
#include "adpcm.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define VIBE_VOICE_HEADER 12
#define VIBE_VOICE_BODY (VIBE_VOICE_HEADER+ADPCM_BLOCK_BYTES)
#define VIBE_VOICE_TYPE 6
#define VIBE_VOICE_MAX_FRAMES 2000u
#define VIBE_VOICE_MAX_REPAIR 4
#define VIBE_VOICE_MAX_HISTORY 30
typedef enum {V_VOICE_LEGACY,V_VOICE_V2,V_VOICE_STREAM} vibe_voice_mode_t;
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
/* Capture always enqueues this canonical body. Only the network owner converts
 * it to an older peer's wire format; no second capture/transmit path exists. */
static inline size_t vibe_voice_datagram(unsigned type,const uint8_t *body,size_t length,
                                         uint8_t *out,size_t capacity) {
    if (!body || !out || length!=VIBE_VOICE_BODY) return 0;
    if (type==VIBE_VOICE_TYPE) {
        if (capacity<VIBE_VOICE_BODY+1) return 0;
        out[0]=VIBE_VOICE_TYPE;memcpy(out+1,body,length);
        return length+1;
    }
    if (type==1 && capacity>=ADPCM_BLOCK_BYTES+3) {
        out[0]=1;out[1]=body[8];out[2]=0x80;
        memcpy(out+3,body+VIBE_VOICE_HEADER,ADPCM_BLOCK_BYTES);
        return ADPCM_BLOCK_BYTES+3;
    }
    return 0;
}
