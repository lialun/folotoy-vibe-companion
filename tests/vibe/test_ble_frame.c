#include "ble_frame.h"
#include "ble_links.h"
#include "ble_tx_budget.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(vibe_ble_tx_admit(6,4,true));
    assert(!vibe_ble_tx_admit(20,3,true)); /* Another pool's free blocks cannot hide exhaustion. */
    assert(!vibe_ble_tx_admit(5,5,true));
    assert(vibe_ble_tx_admit(3,2,false)); /* Control can use the protected reserve. */
    assert(!vibe_ble_tx_admit(2,0,false));
    assert(vibe_ble_tx_wait(100,129,true) && !vibe_ble_tx_wait(100,130,true));
    assert(vibe_ble_tx_wait(100,149,false) && !vibe_ble_tx_wait(100,150,false));
    assert(!vibe_ble_tx_wait(100,99,true));
    assert(vibe_ble_tx_lock_ms(true)==30 && vibe_ble_tx_lock_ms(false)==50);
    assert(!vibe_ble_tx_wait(100,100+vibe_ble_tx_lock_ms(true),true));
    /* Simulate a stalled controller: repeated voice submissions stop before
     * small-pool exhaustion and a write response can still be allocated. */
    unsigned available=12;
    for(unsigned i=0;i<1000;i++) {
        if(available>=3 && vibe_ble_tx_admit(available-3,available-3,true)) available-=3;
        assert(available>=4);
    }
    assert(available==6 && available>=3); /* Request + response headroom remains. */
    available+=3;assert(vibe_ble_tx_admit(available-3,available-3,true));
    vibe_ble_links_t registry;vibe_ble_links_init(&registry);
    /* Reproduce macOS bond restoration: subscription precedes CONNECT. */
    vibe_ble_link_subscription(&registry,1,15,15,true);
    vibe_ble_link_t *early=vibe_ble_link_find(&registry,1);assert(early && early->subscribed);
    vibe_ble_link_subscription(&registry,1,4,15,false);
    assert(early->subscribed); /* Service Changed indication cannot disable TX. */
    vibe_ble_link_subscription(&registry,1,15,15,false);assert(!early->subscribed);
    vibe_ble_link_subscription(&registry,1,15,15,true);assert(early->subscribed);
    vibe_ble_link_subscription(&registry,2,4,15,true);
    assert(!vibe_ble_link_find(&registry,2)); /* Foreign events cannot create slots. */
    uint32_t first_generation=early->generation;early->incoming.used=42;
    vibe_ble_link_t *connected=vibe_ble_link_ensure(&registry,1);
    assert(connected==early && connected->subscribed && connected->generation==first_generation && connected->incoming.used==42);
    assert(vibe_ble_link_ensure(&registry,2));assert(!vibe_ble_link_ensure(&registry,3));
    vibe_ble_link_remove(&registry,1);assert(!vibe_ble_link_find(&registry,1));
    vibe_ble_link_subscription(&registry,1,15,15,false);
    assert(!vibe_ble_link_find(&registry,1));
    assert(vibe_ble_link_ensure(&registry,3));
    vibe_ble_link_remove(&registry,3);
    connected=vibe_ble_link_ensure(&registry,1);
    assert(connected && !connected->subscribed && !connected->incoming.used && connected->generation!=first_generation);
    assert(!vibe_ble_link_ensure(&registry,UINT16_MAX));

    const unsigned lengths[]={1,2,17,804,1537};
    for (unsigned l=0;l<5;l++) for (unsigned limit=20;limit<248;limit+=17) {
        vibe_ble_frame_t state={0};unsigned total=lengths[l];
        for (unsigned offset=0;offset<total;) {
            uint8_t frame[247];unsigned n=total-offset;if (n>limit-7) n=limit-7;
            frame[0]=0x56;frame[1]=255;frame[2]=(offset==0?1:0)|(offset+n==total?2:0);
            frame[3]=total&255;frame[4]=total>>8;frame[5]=offset&255;frame[6]=offset>>8;
            for (unsigned i=0;i<n;i++) frame[i+7]=(uint8_t)(offset+i);
            bool complete=vibe_ble_frame_feed(&state,frame,n+7,1000);
            assert(complete==(offset+n==total));offset+=n;
        }
        for (unsigned i=0;i<total;i++) assert(state.bytes[i]==(uint8_t)i);
    }
    vibe_ble_frame_t state={0};uint8_t part[]={0x56,1,1,2,0,0,0,7},end[]={0x56,1,2,2,0,1,0,8};
    assert(!vibe_ble_frame_feed(&state,part,sizeof(part),1000));
    assert(!vibe_ble_frame_feed(&state,end,sizeof(end),3001)); /* Expired. */
    assert(!vibe_ble_frame_feed(&state,part,sizeof(part),4000));end[5]=0;
    assert(!vibe_ble_frame_feed(&state,end,sizeof(end),4001)); /* Overlap. */
    part[3]=255;part[4]=255;assert(!vibe_ble_frame_feed(&state,part,sizeof(part),5000));
    uint32_t random=0x12345678;vibe_ble_frame_t fuzz={0};
    for (unsigned attempt=0;attempt<100000;attempt++) {
        uint8_t bytes[300];random=random*1664525+1013904223;size_t count=random%sizeof(bytes);
        for (size_t j=0;j<count;j++) { random=random*1664525+1013904223;bytes[j]=(uint8_t)(random>>24); }
        if (attempt%3==0 && count>=7) bytes[0]=0x56;
        (void)vibe_ble_frame_feed(&fuzz,bytes,count,attempt*5ULL);
        assert(fuzz.used<=VIBE_BLE_PACKET_MAX);
    }
    puts("BLE registry/framing: restored subscription order, handle reuse, MTU boundaries, audio/control sizes, overflow, overlap and timeout PASS");
}
