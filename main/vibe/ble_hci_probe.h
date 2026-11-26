#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {
    atomic_uint packets,bytes,completed,failures,last_completed,max_gap;
    atomic_uint processed,max_host_lag,max_process,untracked;
    struct {atomic_uintptr_t address;atomic_uint at;} pending[8];
    atomic_bool recording;
} vibe_hci_probe_t;
static inline void vibe_hci_probe_record(vibe_hci_probe_t *p,bool active,unsigned now) {
    atomic_store(&p->recording,active);
    if(active){atomic_store(&p->last_completed,now);atomic_store(&p->max_gap,0);atomic_store(&p->max_host_lag,0);atomic_store(&p->max_process,0);}
}
static inline void vibe_hci_probe_tx(vibe_hci_probe_t *p,unsigned bytes,int result) {
    if(result){atomic_fetch_add(&p->failures,1);return;}
    if(!bytes)return;
    atomic_fetch_add(&p->packets,1);atomic_fetch_add(&p->bytes,bytes);
}
static inline unsigned vibe_hci_completed(const uint8_t *event,size_t length) {
    /* HCI Number Of Completed Packets, not NimBLE NOTIFY_TX (submission only).
     * Never retain or modify the controller event. */
    if(length<3 || event[0]!=0x13 || event[1]+2u!=length || event[1]!=1u+4u*event[2])return 0;
    unsigned completed=0;
    for(unsigned i=0;i<event[2];i++)completed+=(unsigned)event[5+i*4]|((unsigned)event[6+i*4]<<8);
    return completed;
}
static inline void vibe_hci_probe_event(vibe_hci_probe_t *p,const uint8_t *event,size_t length,unsigned now) {
    unsigned completed=vibe_hci_completed(event,length);
    atomic_fetch_add(&p->completed,completed);
    if(completed) {
        bool tracked=false;
        for(unsigned i=0;i<8;i++) {
            if(atomic_load(&p->pending[i].address)==(uintptr_t)event) {
                /* A reused buffer whose previous event never reached the
                 * host must not inherit its old timestamp. */
                atomic_store(&p->pending[i].at,now);atomic_fetch_add(&p->untracked,1);tracked=true;break;
            }
        }
        for(unsigned i=0;!tracked && i<8;i++) {
            uintptr_t empty=0;
            if(atomic_compare_exchange_strong(&p->pending[i].address,&empty,(uintptr_t)event)) {
                atomic_store(&p->pending[i].at,now);tracked=true;break;
            }
        }
        if(!tracked)atomic_fetch_add(&p->untracked,1);
    }
    if(completed && atomic_load(&p->recording)) {
        unsigned previous=atomic_exchange(&p->last_completed,now);
        unsigned gap=now-previous,maximum=atomic_load(&p->max_gap);
        while(gap>maximum && !atomic_compare_exchange_weak(&p->max_gap,&maximum,gap)){}
    }
}
static inline void vibe_hci_probe_duration(vibe_hci_probe_t *p,unsigned duration) {
    unsigned maximum=atomic_load(&p->max_process);
    while(duration>maximum && !atomic_compare_exchange_weak(&p->max_process,&maximum,duration)){}
}
static inline void vibe_hci_probe_processed(vibe_hci_probe_t *p,const uint8_t *event,size_t length,unsigned now) {
    unsigned completed=vibe_hci_completed(event,length);
    if(!completed)return;
    atomic_fetch_add(&p->processed,completed);
    for(unsigned i=0;i<8;i++) {
        if(atomic_load(&p->pending[i].address)!=(uintptr_t)event)continue;
        unsigned lag=now-atomic_load(&p->pending[i].at);
        atomic_store(&p->pending[i].address,0);
        if(atomic_load(&p->recording)) {
            unsigned maximum=atomic_load(&p->max_host_lag);
            while(lag>maximum && !atomic_compare_exchange_weak(&p->max_host_lag,&maximum,lag)){}
        }
        break;
    }
}
