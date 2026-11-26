#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Explicit little-endian fields; the existing credential remains mandatory. */
enum { V_VOICE_FEEDBACK_TYPE=7, V_VOICE_FEEDBACK_BYTES=54, V_VOICE_FEEDBACK_WINDOW=32 };
typedef struct { uint64_t session;uint32_t id,retire_before,received;bool finished; } vibe_voice_feedback_t;
static inline uint32_t vibe_feedback_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline bool vibe_voice_feedback_parse(const uint8_t *p,size_t size,vibe_voice_feedback_t *value) {
    if (!p || !value || size!=V_VOICE_FEEDBACK_BYTES || p[0]!=V_VOICE_FEEDBACK_TYPE || (p[1]&0x7f)!=1) return false;
    *value=(vibe_voice_feedback_t){.id=vibe_feedback_u32(p+42),.retire_before=vibe_feedback_u32(p+46),.received=vibe_feedback_u32(p+50),.finished=(p[1]&0x80)!=0};
    for (unsigned i=0;i<8;i++) value->session|=(uint64_t)p[34+i]<<(8*i);
    return value->session && value->id;
}
static inline bool vibe_voice_feedback_authenticated(const uint8_t *p,const char *token) {
    if (!p || !token || strlen(token)!=64) return false;
    unsigned different=0;
    for (unsigned i=0;i<32;i++) {
        unsigned byte=0;
        for (unsigned j=0;j<2;j++) {
            unsigned char c=(unsigned char)token[2*i+j];
            unsigned n=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:16;
            if (n>15) return false;
            byte=(byte<<4)|n;
        }
        different|=byte^p[2+i];
    }
    return different==0;
}
