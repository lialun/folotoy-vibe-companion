/* Exercise ownership when producer buffers change, queues fill, or RAM runs out. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "messages.h"
static unsigned allocations;
static bool fail_allocate,full;
static vibe_msg_t queued;
static vibe_outbound_t outbound;
static void *queued_ble;
static void *allocate(size_t size) {
    if (fail_allocate) return NULL;
    void *p=malloc(size);if (p) ++allocations;return p;
}
static void release(void *p) { if (p) { assert(allocations);--allocations;free(p); } }
#define malloc allocate
#define free release
#include "../../main/vibe/messages.c"
#include "ble_ingress.h"
#undef malloc
#undef free
int xQueueSend(QueueHandle_t queue,const void *message,unsigned timeout) {
    assert((queue==(void *)1 || queue==(void *)2 || queue==(void *)3) && timeout==0);
    if (queue==(void *)3) {if(full)return 0;outbound=*(const vibe_outbound_t *)message;return pdTRUE;}
    if (queue==(void *)2) { if (full) return 0;queued_ble=*(void *const *)message;return pdTRUE; }
    if (full) return 0;
    queued=*(const vibe_msg_t *)message;return pdTRUE;
}
int main(void) {
    /* Twelve key slots no longer permanently consume about 18 KiB of JSON RAM. */
    assert(sizeof(vibe_msg_t)<=24);
    char input[]="{\"value\":123}";
    assert(vibe_message_post_json((void *)1,input,strlen(input)));
    assert(allocations==1 && queued.type==V_MSG_JSON);
    input[2]='X';assert(!strcmp(queued.json,"{\"value\":123}"));
    vibe_message_release(&queued);assert(!allocations && !queued.json);
    vibe_message_release(&queued);assert(!allocations);
    assert(vibe_message_post_link_json((void *)1,"{}",2,42));assert(queued.link_generation==42);vibe_message_release(&queued);
    full=true;assert(!vibe_message_post_json((void *)1,"{}",2) && !allocations);full=false;
    fail_allocate=true;assert(!vibe_message_post_json((void *)1,"{}",2) && !allocations);fail_allocate=false;
    assert(!vibe_message_post_json(NULL,"{}",2));
    assert(!vibe_message_post_json((void *)1,NULL,2));
    assert(!vibe_message_post_json((void *)1,"{}",0));
    assert(!vibe_message_post_json((void *)1,"\0{}",3));
    char maximum[VIBE_JSON_MAX+1];memset(maximum,'x',sizeof(maximum));
    assert(!vibe_message_post_json((void *)1,maximum,sizeof(maximum)));
    assert(vibe_message_post_json((void *)1,maximum,VIBE_JSON_MAX));
    assert(strlen(queued.json)==VIBE_JSON_MAX);vibe_message_release(&queued);
    vibe_msg_t key={.type=V_MSG_KEY,.json=(void *)1};vibe_message_release(&key);
    assert(key.json==(void *)1 && !allocations);
    uint8_t ble[]={2,'{','}'};
    assert(vibe_ble_ingress_post((void *)2,7,42,ble,sizeof(ble)) && allocations==1);
    ble[1]='X';vibe_ble_ingress_t *rx=queued_ble;
    assert(rx->handle==7 && rx->generation==42 && rx->length==3 && rx->bytes[1]=='{');release(rx);assert(!allocations);
    full=true;assert(!vibe_ble_ingress_post((void *)2,7,42,ble,sizeof(ble)) && !allocations);full=false;
    fail_allocate=true;assert(!vibe_ble_ingress_post((void *)2,7,42,ble,sizeof(ble)) && !allocations);fail_allocate=false;
    assert(!vibe_ble_ingress_post(NULL,7,42,ble,sizeof(ble)));
    assert(!vibe_ble_ingress_post((void *)2,UINT16_MAX,42,ble,sizeof(ble)));
    assert(!vibe_ble_ingress_post((void *)2,7,42,ble,1));
    assert(!vibe_ble_ingress_post((void *)2,7,42,maximum,VIBE_JSON_MAX+2));
    ble[0]=1;assert(!vibe_ble_ingress_post((void *)2,7,42,ble,sizeof(ble)));
    uint8_t feedback[V_VOICE_FEEDBACK_BYTES]={V_VOICE_FEEDBACK_TYPE,1};
    assert(vibe_ble_ingress_post((void *)2,7,42,feedback,sizeof(feedback)) && allocations==1);
    rx=queued_ble;assert(rx->length==sizeof(feedback) && rx->bytes[0]==V_VOICE_FEEDBACK_TYPE);release(rx);
    assert(!vibe_ble_ingress_post((void *)2,7,42,feedback,sizeof(feedback)-1) && !allocations);
    full=true;assert(!vibe_ble_ingress_post((void *)2,7,42,feedback,sizeof(feedback)) && !allocations);full=false;
    assert(vibe_ble_ingress_disconnect((void *)2,7) && allocations==1);
    rx=queued_ble;assert(rx->length==0 && rx->handle==7);release(rx);
    full=true;assert(!vibe_ble_ingress_disconnect((void *)2,7) && !allocations);full=false;
    assert(vibe_outbound_post((void *)3,"{\"event\":\"voice.start\"}",7,V_DELIVERY_CRITICAL) && allocations==1);
    assert(outbound.owner==7 && vibe_outbound_current(&outbound,7,false));
    assert(!vibe_outbound_current(&outbound,8,false) && !vibe_outbound_current(&outbound,7,true));
    release(outbound.json);
    assert(vibe_outbound_post((void *)3,"{}",0,V_DELIVERY_PRIMARY));
    assert(vibe_outbound_current(&outbound,8,true));release(outbound.json);
    full=true;assert(!vibe_outbound_post((void *)3,"{}",7,V_DELIVERY_BOTH) && !allocations);full=false;
    fail_allocate=true;assert(!vibe_outbound_post((void *)3,"{}",7,V_DELIVERY_BOTH) && !allocations);fail_allocate=false;
    puts("BLE callback ingress: deferred copy, source, bounds, full queue and allocation rollback PASS");
    puts("Compact message queue: ownership, bounds, full queue and allocation failure PASS");
}
