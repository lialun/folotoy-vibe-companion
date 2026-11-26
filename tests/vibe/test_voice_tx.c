#include "voice_tx.h"
#include <assert.h>
#include <stdio.h>

static void frame(uint8_t *body, uint64_t session, uint32_t sequence) {
    uint8_t block[ADPCM_BLOCK_BYTES]={0};
    assert(vibe_voice_encode(session,sequence,block,sizeof(block),body,VIBE_VOICE_BODY)==VIBE_VOICE_BODY);
}

static void independent_legs(unsigned failing_leg) {
    vibe_voice_tx_t tx;
    vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
    uint8_t input[VIBE_VOICE_BODY], output[VIBE_VOICE_BODY];
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,1,7);
    unsigned good=0,failed_attempts=0;
    for (unsigned tick=0;tick<3000;tick++) {
        uint64_t now=tick*20;
        if (tick%5==0) {
            frame(input,1,tick/5);
            assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,now));
        }
        for (unsigned leg=0;leg<2;leg++) {
            vibe_voice_tx_ticket_t ticket;
            if (!vibe_voice_tx_take(&tx,leg,now,output,&ticket)) continue;
            assert(vibe_voice_sequence(output)==ticket.sequence);
            if (leg!=failing_leg) {assert(ticket.sequence==good);++good;} else ++failed_attempts;
            vibe_voice_tx_complete(&tx,&ticket,now,leg!=failing_leg ? V_VOICE_SUBMITTED:V_VOICE_NOT_SUBMITTED);
        }
    }
    assert(good==600 && tx.sent[failing_leg^1]==600 && tx.sent[failing_leg]==0);
    assert(failed_attempts<=3000 && tx.failed==failed_attempts && tx.expired>0);
}

static void recovery_window(void) {
    vibe_voice_tx_t tx;
    vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
    uint8_t input[VIBE_VOICE_BODY],output[VIBE_VOICE_BODY];
    vibe_voice_tx_ticket_t ticket;
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,4,7);
    frame(input,4,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,0));
    unsigned failures=0,delivered=0;
    for(unsigned now=0;now<=250;now++) {
        if(vibe_voice_tx_take(&tx,0,now,output,&ticket)) {
            bool recovered=now>=180;
            failures+=!recovered;delivered+=recovered;
            vibe_voice_tx_complete(&tx,&ticket,now,recovered ? V_VOICE_SUBMITTED:V_VOICE_NOT_SUBMITTED);
        }
    }
    assert(delivered==1 && failures==4); /* Does not exhaust 8 tries by 140 ms. */
    assert(vibe_voice_tx_take(&tx,1,250,output,&ticket) && ticket.sequence==0);
    vibe_voice_tx_complete(&tx,&ticket,250,V_VOICE_SUBMITTED); /* Other carrier remains independent. */

    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,5,7);
    frame(input,5,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,1,0));
    assert(vibe_voice_tx_take(&tx,0,0,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,0,V_VOICE_SUBMITTED);
    frame(input,5,5);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,1,500));
    assert(vibe_voice_tx_repair(&tx,5,0,1,500,1));
    assert(vibe_voice_tx_take(&tx,0,500,output,&ticket) && ticket.repair && ticket.sequence==0);
    vibe_voice_tx_complete(&tx,&ticket,500,V_VOICE_NOT_SUBMITTED);
    frame(input,5,7);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,1,510));
    assert(!vibe_voice_tx_take(&tx,0,510,output,&ticket)); /* Fresh capture cannot bypass carrier backoff. */
    assert(vibe_voice_tx_take(&tx,0,520,output,&ticket) && ticket.sequence==0);
    vibe_voice_tx_complete(&tx,&ticket,520,V_VOICE_SUBMITTED);
    assert(vibe_voice_tx_take(&tx,0,520,output,&ticket) && !ticket.repair);
}

static void first_submission_budget(void) {
    for(unsigned links=1;links<=3;links++) {
        vibe_voice_tx_t tx;
        vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
        uint8_t input[VIBE_VOICE_BODY],output[VIBE_VOICE_BODY];
        vibe_voice_tx_ticket_t ticket;
        vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,9,7);
        frame(input,9,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,links,0));
        unsigned leg=links==2 ? 1:0,delivered=0;
        for(unsigned now=0;now<=500;now++) {
            if(!vibe_voice_tx_take(&tx,leg,now,output,&ticket)) continue;
            bool recovered=now>=360;
            delivered+=recovered;
            vibe_voice_tx_complete(&tx,&ticket,now,recovered ? V_VOICE_SUBMITTED:V_VOICE_NOT_SUBMITTED);
        }
        assert(delivered==1 && tx.sent[leg]==1 && !slots[0].received);
        if(links==3) assert(vibe_voice_tx_take(&tx,1,501,output,&ticket));
    }
    /* Neither carrier accepting a frame must not manufacture a fresh budget.
     * Its existing slot/deadline still bounds outage work and memory. */
    vibe_voice_tx_t tx;
    vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
    uint8_t input[VIBE_VOICE_BODY],output[VIBE_VOICE_BODY];
    vibe_voice_tx_ticket_t ticket;
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,10,7);
    frame(input,10,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,2,0));
    assert(!vibe_voice_tx_take(&tx,1,601,output,&ticket) && !vibe_voice_tx_pending(&tx));
}

static void partial_assembly_order(void) {
    for(unsigned partial=0;partial<2;partial++) {
        vibe_voice_tx_t tx;
        vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
        uint8_t input[VIBE_VOICE_BODY],output[VIBE_VOICE_BODY];
        vibe_voice_tx_ticket_t ticket;
        vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,20,7);
        frame(input,20,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,0));
        for(unsigned leg=0;leg<2;leg++) {
            assert(vibe_voice_tx_take(&tx,leg,0,output,&ticket));
            vibe_voice_tx_complete(&tx,&ticket,0,V_VOICE_SUBMITTED);
        }
        frame(input,20,3);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,300));
        assert(vibe_voice_tx_take(&tx,1,300,output,&ticket));
        vibe_voice_tx_complete(&tx,&ticket,300,partial ? V_VOICE_PARTIAL:V_VOICE_NOT_SUBMITTED);
        assert(vibe_voice_tx_repair(&tx,20,0,3,310,1));
        assert(vibe_voice_tx_take(&tx,0,310,output,&ticket) && ticket.sequence==0);
        vibe_voice_tx_complete(&tx,&ticket,310,V_VOICE_SUBMITTED); /* Other carrier keeps EDF. */
        if(partial) {
            assert(vibe_voice_tx_take(&tx,1,310,output,&ticket) && ticket.sequence==3);
            /* No new credit does not invalidate the prefix already at the peer. */
            vibe_voice_tx_complete(&tx,&ticket,310,V_VOICE_NOT_SUBMITTED);
            assert(!vibe_voice_tx_take(&tx,1,329,output,&ticket));
            assert(vibe_voice_tx_take(&tx,1,330,output,&ticket) && ticket.sequence==3);
            vibe_voice_tx_complete(&tx,&ticket,330,V_VOICE_SUBMITTED);
            assert(vibe_voice_tx_take(&tx,1,330,output,&ticket) && ticket.sequence==0);
        } else {
            assert(!vibe_voice_tx_take(&tx,1,310,output,&ticket));
            assert(vibe_voice_tx_take(&tx,1,320,output,&ticket) && ticket.sequence==0);
        }
        vibe_voice_tx_complete(&tx,&ticket,360,V_VOICE_SUBMITTED);
        assert(!(tx.leased&V_VOICE_BLE));
    }
    vibe_voice_tx_t tx;
    vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
    uint8_t input[VIBE_VOICE_BODY],output[VIBE_VOICE_BODY];
    vibe_voice_tx_ticket_t ticket,old;
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,21,7);
    frame(input,21,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,2,0));
    assert(vibe_voice_tx_take(&tx,1,0,output,&old));
    vibe_voice_tx_complete(&tx,&old,0,V_VOICE_PARTIAL);
    frame(input,21,1);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,2,500));
    assert(vibe_voice_tx_take(&tx,1,601,output,&ticket) && ticket.sequence==1 && !tx.leased);
    vibe_voice_tx_complete(&tx,&ticket,601,V_VOICE_PARTIAL);
    frame(input,21,7);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,2,650));
    assert(vibe_voice_tx_take(&tx,1,650,output,&ticket) && ticket.sequence==7 && !tx.leased);
    vibe_voice_tx_complete(&tx,&ticket,650,V_VOICE_PARTIAL);
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,22,7);
    vibe_voice_tx_complete(&tx,&ticket,660,V_VOICE_PARTIAL);
    assert(!tx.leased && !vibe_voice_tx_pending(&tx));
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,23,7);
    frame(input,23,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,0));
    assert(vibe_voice_tx_take(&tx,1,0,output,&ticket));
    vibe_voice_tx_complete(&tx,&ticket,0,V_VOICE_PARTIAL);
    assert(vibe_voice_tx_take(&tx,0,10,output,&ticket));
    vibe_voice_tx_complete(&tx,&ticket,10,V_VOICE_SUBMITTED);
    assert(vibe_voice_tx_take(&tx,1,251,output,&ticket) && tx.leased);
    vibe_voice_tx_complete(&tx,&ticket,251,V_VOICE_PARTIAL);
    assert(!vibe_voice_tx_take(&tx,1,601,output,&ticket) && !tx.leased);
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,24,7);
    frame(input,24,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,2,0));
    for(unsigned now=0;now<=600;now++) {
        if(vibe_voice_tx_take(&tx,1,now,output,&ticket))
            vibe_voice_tx_complete(&tx,&ticket,now,now==0 ? V_VOICE_PARTIAL:V_VOICE_NOT_SUBMITTED);
    }
    assert(!tx.leased && !vibe_voice_tx_pending(&tx) && slots[0].tries[1]==V_VOICE_MAX_TRIES);
}

static void credit_progress(void) {
    vibe_voice_tx_t tx;
    vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
    uint8_t input[VIBE_VOICE_BODY],output[VIBE_VOICE_BODY];
    vibe_voice_tx_ticket_t ticket;
    unsigned fragments[120]={0},sent=0;
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,25,7);
    for(unsigned now=0;now<13000;now+=20) {
        if(now<12000 && now%100==0) {
            frame(input,25,now/100);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,2,now));
        }
        if(!vibe_voice_tx_take(&tx,1,now,output,&ticket)) continue;
        bool complete=++fragments[ticket.sequence]==4;
        vibe_voice_tx_complete(&tx,&ticket,now,complete ? V_VOICE_SUBMITTED:V_VOICE_PARTIAL);
        sent+=complete;
    }
    assert(sent==120 && tx.overwritten==0 && tx.expired==0 && tx.failed==0);
}

int main(void) {
    independent_legs(0);
    independent_legs(1);
    recovery_window();
    first_submission_budget();
    partial_assembly_order();
    credit_progress();
    vibe_voice_tx_t tx;
    vibe_voice_tx_slot_t slots[V_VOICE_TX_SLOTS];
    uint8_t input[VIBE_VOICE_BODY], output[VIBE_VOICE_BODY];
    vibe_voice_tx_ticket_t old,ticket;
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,1,7);
    frame(input,1,0);
    assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,0));
    assert(vibe_voice_tx_take(&tx,0,0,output,&old));
    vibe_voice_tx_complete(&tx,&old,0,V_VOICE_NOT_SUBMITTED);
    assert(!vibe_voice_tx_take(&tx,0,19,output,&ticket));
    assert(vibe_voice_tx_take(&tx,0,20,output,&ticket) && ticket.sequence==0);
    for (unsigned seq=1;seq<=6;seq++) {
        frame(input,1,seq);
        assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,seq*100));
    }
    vibe_voice_tx_complete(&tx,&old,600,V_VOICE_NOT_SUBMITTED);
    assert(slots[0].sequence==6 && slots[0].tries[0]==0);
    assert(!vibe_voice_tx_repair(&tx,1,0,3,600,0));
    assert(vibe_voice_tx_repair(&tx,1,5,3,600,0));
    assert(!vibe_voice_tx_repair(&tx,1,5,3,601,0));
    assert(vibe_voice_tx_take(&tx,0,600,output,&ticket));
    assert(!ticket.repair && ticket.sequence==1); /* Oldest undelivered frame first. */
    vibe_voice_tx_complete(&tx,&ticket,600,V_VOICE_SUBMITTED);
    vibe_voice_tx_end(&tx,1,8,601);
    assert(tx.recording); /* Old ownership cannot stop capture. */
    vibe_voice_tx_end(&tx,1,7,601);
    assert(!tx.recording && vibe_voice_tx_live(&tx,7,1200));
    assert(!vibe_voice_tx_live(&tx,8,602) && !vibe_voice_tx_live(&tx,7,1201));
    assert(!vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,602));
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,2,8);
    vibe_voice_tx_complete(&tx,&ticket,603,V_VOICE_SUBMITTED);
    assert(tx.sent[0]==0 && !vibe_voice_tx_pending(&tx));
    frame(input,2,0);
    assert(!vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,700));
    assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),8,2,700));
    assert(!vibe_voice_tx_take(&tx,0,700,output,&ticket));
    assert(vibe_voice_tx_take(&tx,1,700,output,&ticket));
    vibe_voice_tx_complete(&tx,&ticket,700,V_VOICE_SUBMITTED);
    assert(tx.sent[1]==1 && !vibe_voice_tx_pending(&tx));
    assert(vibe_voice_parse_session("00000000000000ab")==171);
    assert(!vibe_voice_parse_session("ABC") && !vibe_voice_parse_session("00000000000000AB"));
    /* The same logical NACK can arrive later on the other carrier after its
     * first repair completed. It must not spend another request allowance. */
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,3,7);
    frame(input,3,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,0));
    for(unsigned leg=0;leg<2;leg++) {assert(vibe_voice_tx_take(&tx,leg,0,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,0,V_VOICE_SUBMITTED);}
    assert(vibe_voice_tx_repair(&tx,3,0,3,20,1));
    for(unsigned leg=0;leg<2;leg++) {assert(vibe_voice_tx_take(&tx,leg,20,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,20,V_VOICE_SUBMITTED);}
    assert(!vibe_voice_tx_repair(&tx,3,0,3,40,1) && slots[0].requests==1);
    assert(vibe_voice_tx_repair(&tx,3,0,3,140,2) && slots[0].requests==2);
    assert(!vibe_voice_tx_repair(&tx,3,0,3,160,1));
    for(unsigned leg=0;leg<2;leg++) {assert(vibe_voice_tx_take(&tx,leg,160,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,160,V_VOICE_SUBMITTED);}
    assert(!vibe_voice_tx_repair(&tx,3,0,3,180,2));
    vibe_voice_tx_begin(&tx,slots,V_VOICE_TX_SLOTS,6,7);
    frame(input,6,0);assert(vibe_voice_tx_enqueue(&tx,input,sizeof(input),7,3,0));
    for(unsigned leg=0;leg<2;leg++) {assert(vibe_voice_tx_take(&tx,leg,0,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,0,V_VOICE_SUBMITTED);}
    assert(vibe_voice_tx_repair(&tx,6,0,3,20,1));
    assert(vibe_voice_tx_take(&tx,0,20,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,20,V_VOICE_SUBMITTED);
    assert(vibe_voice_tx_repair(&tx,6,0,3,140,2)); /* BLE remains pending, Wi-Fi can retry. */
    assert(slots[0].tries[1]==0 && slots[0].requests==2);
    assert(vibe_voice_tx_take(&tx,0,140,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,140,V_VOICE_SUBMITTED);
    assert(vibe_voice_tx_take(&tx,1,140,output,&ticket));vibe_voice_tx_complete(&tx,&ticket,140,V_VOICE_SUBMITTED);
    assert(!vibe_voice_tx_repair(&tx,6,0,3,160,2));
    puts("Voice TX: independent carrier recovery, bounded backoff, expiry priority, repair and ownership PASS");
}
