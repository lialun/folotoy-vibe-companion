#include "voice_tx.h"
#include <assert.h>
#include <stdio.h>

static void enqueue(vibe_voice_tx_t *tx,uint32_t sequence) {
    uint8_t block[ADPCM_BLOCK_BYTES]={0},body[VIBE_VOICE_BODY];
    assert(vibe_voice_encode(tx->session,sequence,block,sizeof(block),body,sizeof(body))==sizeof(body));
    assert(vibe_voice_tx_enqueue(tx,body,sizeof(body),7,3,100*sequence));
}
/* A healthy carrier must deliver every new capture while a sibling makes
 * partial/no progress. Lost feedback can increase work, never stall capture. */
static void healthy_carrier_with_feedback(void) {
    for(unsigned capacity=6;capacity<=30;capacity+=12)for(unsigned healthy=0;healthy<2;healthy++) {
        vibe_voice_tx_t tx;vibe_voice_tx_slot_t slots[30];
        bool delivered[200]={0};unsigned count=0;
        vibe_voice_tx_begin(&tx,slots,capacity,123,7);tx.feedback=true;tx.retention_ms=capacity*100;
        for(unsigned tick=0;tick<1000;tick++) {
            uint64_t now=tick*20;
            if(tick%5==0)enqueue(&tx,tick/5);
            for(unsigned leg=0;leg<2;leg++) {
                uint8_t body[VIBE_VOICE_BODY];vibe_voice_tx_ticket_t ticket;
                if(!vibe_voice_tx_take(&tx,leg,now,body,&ticket))continue;
                assert(vibe_voice_sequence(body)==ticket.sequence);
                if(leg==healthy) {
                    assert(ticket.sequence==count && !delivered[count]);
                    delivered[count++]=true;
                }
                vibe_voice_tx_complete(&tx,&ticket,now,leg==healthy?V_VOICE_SUBMITTED:
                    tick%3==0?V_VOICE_PARTIAL:V_VOICE_NOT_SUBMITTED);
            }
            if(tick%10==0 && tick%50!=0) {
                vibe_voice_feedback_t f={.session=123,.id=tick+1,.retire_before=count};
                assert(vibe_voice_tx_feedback(&tx,&f,7));
                for(unsigned i=0;i<capacity;i++)
                    assert(!slots[i].valid || slots[i].sequence>=count);
            }
        }
        assert(count==200 && tx.sent[healthy]==200 && tx.recording);
    }
}
int main(void) {
    healthy_carrier_with_feedback();
    vibe_voice_tx_t tx;vibe_voice_tx_slot_t slots[30];
    vibe_voice_tx_begin(&tx,slots,30,123,7);tx.feedback=true;tx.retention_ms=3000;
    for (unsigned i=0;i<8;i++) enqueue(&tx,i);
    uint8_t body[VIBE_VOICE_BODY];vibe_voice_tx_ticket_t old;
    assert(vibe_voice_tx_take(&tx,1,800,body,&old) && old.sequence==0);
    vibe_voice_tx_complete(&tx,&old,800,V_VOICE_PARTIAL);
    vibe_voice_feedback_t f={.session=123,.id=1,.received=(1u<<0)|(1u<<1)|(1u<<3)|(1u<<6)};
    assert(vibe_voice_tx_feedback(&tx,&f,7) && tx.confirmed==4 && !tx.leased);
    vibe_voice_tx_complete(&tx,&old,801,V_VOICE_NOT_SUBMITTED);
    assert(slots[0].pending==0 && !vibe_voice_tx_repair(&tx,123,0,3,801,1));
    assert(!vibe_voice_tx_feedback(&tx,&f,7));
    f.id=2;f.retire_before=4;f.received=1u<<2;
    assert(vibe_voice_tx_feedback(&tx,&f,7) && !slots[2].valid && tx.retired==1);
    f.id=3;f.received=UINT32_C(1)<<31;
    assert(!vibe_voice_tx_feedback(&tx,&f,7) && tx.feedback_id==2);
    f.received=0;f.session=124;assert(!vibe_voice_tx_feedback(&tx,&f,7));
    f.session=123;assert(!vibe_voice_tx_feedback(&tx,&f,8));
    vibe_voice_tx_end(&tx,123,7,900);
    f.retire_before=8;f.received=0;
    assert(vibe_voice_tx_feedback(&tx,&f,7) && vibe_voice_tx_live(&tx,7,901));
    f.id=4;f.finished=true;
    assert(vibe_voice_tx_feedback(&tx,&f,7) && !vibe_voice_tx_live(&tx,7,901));
    vibe_voice_tx_begin(&tx,slots,30,125,7);tx.feedback=true;tx.retention_ms=3000;
    for (unsigned i=0;i<200;i++) enqueue(&tx,i);
    assert(tx.produced==200 && tx.overwritten==170);
    f=(vibe_voice_feedback_t){.session=125,.id=1,.retire_before=170,.received=0x3fffffffu};
    assert(vibe_voice_tx_feedback(&tx,&f,7) && tx.confirmed==30);
    for (unsigned i=0;i<30;i++) assert(slots[i].pending==0);
    uint8_t packet[V_VOICE_FEEDBACK_BYTES]={7,1};char token[65];
    for (unsigned i=0;i<64;i++) token[i]='0';
    token[64]=0;
    packet[34]=1;packet[42]=1;
    assert(vibe_voice_feedback_parse(packet,sizeof(packet),&f));
    assert(vibe_voice_feedback_authenticated(packet,token));
    packet[5]=1;assert(!vibe_voice_feedback_authenticated(packet,token));packet[5]=0;
    packet[1]=0x81;assert(vibe_voice_feedback_parse(packet,sizeof(packet),&f) && f.finished);
    packet[1]=2;assert(!vibe_voice_feedback_parse(packet,sizeof(packet),&f));
    assert(!vibe_voice_feedback_parse(packet,sizeof(packet)-1,&f));
    puts("Receiver feedback: selective receipt, retirement, owner/session, stale completion, bounded ring and authenticated wire PASS");
}
