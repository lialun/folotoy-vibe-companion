#include "voice_history.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    vibe_voice_history_slot_t slots[4];vibe_voice_history_t h={0};
    uint8_t block[ADPCM_BLOCK_BYTES]={0},body[VIBE_VOICE_BODY],out[VIBE_VOICE_BODY];
    vibe_voice_attempt_t a;const uint64_t session=UINT64_C(0x123456789abcdef0);
    assert(vibe_voice_parse_session("123456789abcdef0")==session);
    assert(!vibe_voice_parse_session("0000000000000000") && !vibe_voice_parse_session("123456789ABCDEF0"));
    assert(!vibe_voice_parse_session(NULL) && !vibe_voice_parse_session("1"));
    vibe_voice_history_init(&h,slots,4,session,7);
    for(unsigned i=0;i<4;i++) {
        assert(vibe_voice_encode(session,i,block,sizeof(block),body,sizeof(body))==sizeof(body));
        assert(!vibe_voice_history_store(&h,body,8,i*100));
        assert(vibe_voice_history_store(&h,body,7,i*100));
    }
    assert(!vibe_voice_history_request(&h,session+1,1,310));
    assert(!vibe_voice_history_request(&h,session,5,310));
    assert(vibe_voice_history_request(&h,session,1,310));
    assert(!vibe_voice_history_request(&h,session,1,311));
    assert(vibe_voice_history_take(&h,310,out,&a) && a.kind==V_VOICE_REPAIR && a.sequence==1);
    assert(vibe_voice_sequence(out)==1 && vibe_voice_session(out)==session);
    vibe_voice_history_complete(&h,&a,311,false);
    assert(!vibe_voice_history_take(&h,330,out,&a));
    assert(vibe_voice_history_take(&h,331,out,&a));
    vibe_voice_history_complete(&h,&a,332,true);
    assert(!vibe_voice_history_pending(&h));
    assert(!vibe_voice_history_request(&h,session,1,340));
    assert(vibe_voice_history_request(&h,session,1,371));
    assert(vibe_voice_history_take(&h,371,out,&a));vibe_voice_history_complete(&h,&a,372,true);
    assert(!vibe_voice_history_request(&h,session,1,440)); /* two request tickets */
    assert(vibe_voice_history_request(&h,session,0,380)); /* use the complete retained window */
    assert(vibe_voice_history_take(&h,380,out,&a));vibe_voice_history_complete(&h,&a,381,true);
    assert(!vibe_voice_history_request(&h,session,0,401)); /* original TTL */
    assert(vibe_voice_history_retry_wifi(&h,session,3,310));
    unsigned attempts=0;
    for(unsigned now=320;now<700;now+=20)if(vibe_voice_history_take(&h,now,out,&a)) {
        assert(a.kind==V_VOICE_WIFI_RETRY);++attempts;vibe_voice_history_complete(&h,&a,now,false);
    }
    assert(attempts==8 && !vibe_voice_history_pending(&h));
    assert(vibe_voice_encode(session,7,block,sizeof(block),body,sizeof(body))==sizeof(body));
    assert(vibe_voice_history_store(&h,body,7,700));
    assert(!vibe_voice_history_request(&h,session,3,710)); /* ring overwrite */
    assert(vibe_voice_history_request(&h,session,7,710));
    assert(vibe_voice_history_take(&h,710,out,&a));
    assert(vibe_voice_encode(session,11,block,sizeof(block),body,sizeof(body))==sizeof(body));
    assert(vibe_voice_history_store(&h,body,7,720));
    vibe_voice_history_complete(&h,&a,730,false);assert(!vibe_voice_history_pending(&h));
    h.recording=false;assert(!vibe_voice_history_store(&h,body,7,740));
    vibe_voice_history_init(&h,slots,2,session+1,8);
    assert(!vibe_voice_history_request(&h,session,11,750));
    assert(sizeof(slots)<3600);
    puts("Voice repair: exact session/sequence, bounded ring, original age, request cooldown, retries, stale completion and reset PASS");
}
