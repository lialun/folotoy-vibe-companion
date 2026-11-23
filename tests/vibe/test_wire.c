#include "adpcm.h"
#include "protocol.h"
#include "voice_wire.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(vibe_json_depth_ok("{\"type\":\"config.set\",\"settings\":{\"volume\":0}}",45));
    const char *nested="[[[[[[[[[]]]]]]]]]";
    assert(!vibe_json_depth_ok(nested,strlen(nested)));
    assert(!vibe_json_depth_ok("{\"x\":\"abc",9));
    assert(!vibe_json_depth_ok("}",1));
    int16_t pcm[1600],decoded[1600]; uint8_t encoded[804]; adpcm_state_t st;
    for (int i=0;i<1600;i++) pcm[i]=(int16_t)(12000*sin(i*0.12));
    adpcm_state_reset(&st);
    assert(adpcm_encode_block(&st,pcm,1600,encoded,sizeof(encoded))==804);
    assert(adpcm_decode_block(encoded,804,1600,decoded,1600)==1600);
    assert(!adpcm_decode_block(encoded,803,1600,decoded,1600));
    assert(!adpcm_encode_block(&st,pcm,1600,encoded,803));
    if (argc>1) {
        FILE *f=fopen(argv[1],"wb"); assert(f);
        assert(fwrite(encoded,1,804,f)==804);
        for (int i=0;i<1600;i++) {
            unsigned v=(uint16_t)decoded[i];fputc(v&255,f);fputc(v>>8,f);
        }
        assert(!fclose(f));
    }
    if (argc>2) {
        uint8_t packet[VIBE_VOICE_BODY+1];packet[0]=VIBE_VOICE_TYPE;
        assert(vibe_voice_encode(UINT64_C(0x123456789abcdef0),256,encoded,sizeof(encoded),packet+1,sizeof(packet)-1)==VIBE_VOICE_BODY);
        FILE *f=fopen(argv[2],"wb");assert(f);assert(fwrite(packet,1,sizeof(packet),f)==sizeof(packet));assert(!fclose(f));
    }
    puts("Bounded JSON and firmware ADPCM wire vectors: PASS");
}
