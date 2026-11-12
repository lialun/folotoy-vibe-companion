#include "radio/relay_buffer.h"
#include "adpcm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void number(uint8_t *p,uint32_t n) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
int main(int argc,char **argv) {
    uint8_t segments[3][V_RELAY_SEGMENT_FRAMES*V_RELAY_BYTES],packet[V_RELAY_PACKET]={5},out[V_RELAY_BYTES];
    int16_t pcm[V_RELAY_SAMPLES],decoded[V_RELAY_SAMPLES];
    for (unsigned i=0;i<V_RELAY_SAMPLES;i++) pcm[i]=(int16_t)((int)(i%61)*701-21000);
    adpcm_state_t state;adpcm_state_reset(&state);
    assert(adpcm_encode_block(&state,pcm,V_RELAY_SAMPLES,packet+9,V_RELAY_BYTES)==V_RELAY_BYTES);
    number(packet+1,777);
    for (unsigned count=2;count<=3;count++) {
        vibe_relay_buffer_t b={.segments={segments[0],segments[1],segments[2]},.capacity=count*13,.stream=777};
        for (unsigned cycle=0;cycle<100;cycle++) {
            for (unsigned j=0;j<b.capacity;j++) {
                number(packet+5,b.received);assert(vibe_relay_push(&b,packet,sizeof(packet)));
                assert(!vibe_relay_push(&b,packet,sizeof(packet))); /* duplicate */
            }
            number(packet+5,b.received);assert(!vibe_relay_push(&b,packet,sizeof(packet))); /* full */
            for (unsigned j=0;j<b.capacity;j++) {
                assert(vibe_relay_pop(&b,out));assert(!memcmp(out,packet+9,V_RELAY_BYTES));
                assert(adpcm_decode_block(out,sizeof(out),V_RELAY_SAMPLES,decoded,V_RELAY_SAMPLES)==V_RELAY_SAMPLES);
            }
            assert(!vibe_relay_pop(&b,out));
        }
        number(packet+5,b.received+1);assert(!vibe_relay_push(&b,packet,sizeof(packet))); /* gap */
        number(packet+5,b.received);number(packet+1,778);assert(!vibe_relay_push(&b,packet,sizeof(packet))); /* stale session */
        number(packet+1,777);assert(!vibe_relay_push(&b,packet,sizeof(packet)-1));
        packet[11]=89;assert(!vibe_relay_push(&b,packet,sizeof(packet)));packet[11]=0;
        packet[12]=1;assert(!vibe_relay_push(&b,packet,sizeof(packet)));packet[12]=0;
    }
    if (argc>1) {
        adpcm_state_reset(&state);assert(adpcm_encode_block(&state,pcm,V_RELAY_SAMPLES,out,sizeof(out))==V_RELAY_BYTES);
        FILE *file=fopen(argv[1],"wb");assert(file);
        assert(fwrite(out,1,sizeof(out),file)==sizeof(out));
        for (unsigned i=0;i<V_RELAY_SAMPLES;i++) { fputc((uint16_t)pcm[i]&255,file);fputc((uint16_t)pcm[i]>>8,file); }
        assert(fclose(file)==0);
    }
    puts("Relay: 16/24 KiB segmented ring, 100 wraps, credit overflow, stale/duplicate/gap and malformed blocks PASS");
}
