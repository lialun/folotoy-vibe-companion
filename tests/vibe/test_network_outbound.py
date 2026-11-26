"""Execute the actual network control dispatcher with bounded queue/SDK stubs."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'main/vibe/network.c').read_text()
begin=source.index('static void service_voice(void) {')
end=source.index('\nstatic void ui_provision(',begin)
dispatcher=source[begin:end]
preamble=r'''
#include "messages.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static QueueHandle_t tx_queue=(void*)1;
static SemaphoreHandle_t peer_mutex=(void*)2;
static uint32_t owner_generation=7;
static bool release_after_send,locked,fail_eid,receiver_acknowledged=true;
static unsigned event_sequence,now,serviced,max_gap,submissions,first_eid,maintained;
static vibe_outbound_t items[32];
static unsigned head,tail;
int xQueueReceive(QueueHandle_t queue,void *out,unsigned wait) {
    assert(queue==tx_queue && !wait && !locked);
    if(head==tail)return 0;
    *(vibe_outbound_t*)out=items[head++];return pdTRUE;
}
unsigned uxQueueMessagesWaiting(QueueHandle_t queue) {assert(queue==tx_queue);return tail-head;}
int xSemaphoreTake(SemaphoreHandle_t lock,unsigned wait) {
    (void)wait;assert(lock==peer_mutex && !locked);locked=true;return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock) {
    assert(lock==peer_mutex && locked);locked=false;return pdTRUE;
}
static bool vibe_voice_uplink_can_overtake_control(void) {return receiver_acknowledged;}
static void apply_wifi_power(void) {assert(!locked);}
static void vibe_voice_uplink_service(bool may_transmit) {
    assert(!locked);++maintained;
    if(!may_transmit)return;
    if(now-serviced>max_gap)max_gap=now-serviced;
    serviced=now;
}
static void submit(const char *json,uint32_t owner) {
    assert(!locked && owner==owner_generation && json && !release_after_send);
    unsigned eid=0;
    if(sscanf(json,"{\"eid\":%u}",&eid)==1) {
        if(!first_eid)first_eid=eid;
        assert(eid==first_eid);
    }
    now+=50;++submissions; /* A pressured control send uses its entire budget. */
}
static void send_json_both(const char *json,uint32_t owner) {submit(json,owner);}
static int send_packet(unsigned type,const void *json,size_t n,uint32_t owner) {
    assert(type==2 && n==strlen(json));submit(json,owner);return 0;
}
typedef struct {unsigned eid;} cJSON;
static cJSON *cJSON_Parse(const char *text) {return !strcmp(text,"invalid")?NULL:calloc(1,sizeof(cJSON));}
static cJSON *cJSON_AddNumberToObject(cJSON *o,const char *key,unsigned number) {
    assert(!strcmp(key,"eid"));if(fail_eid)return NULL;o->eid=number;return o;
}
static char *cJSON_PrintUnformatted(cJSON *o) {
    char *line=malloc(32);assert(line);snprintf(line,32,"{\"eid\":%u}",o->eid);return line;
}
static void cJSON_Delete(cJSON *o) {free(o);}
static void enqueue(vibe_delivery_t delivery,uint32_t owner,const char *json) {
    assert(tail<32);char *copy=malloc(strlen(json)+1);assert(copy);strcpy(copy,json);
    items[tail++]=(vibe_outbound_t){.json=copy,.owner=owner,.delivery=delivery};
}
'''
main=r'''
int main(void) {
    for(unsigned i=0;i<8;i++)enqueue(V_DELIVERY_PRIMARY,7,"{}");
    assert(!send_outbound());assert(head==4 && submissions==4 && max_gap==50);
    assert(send_outbound());assert(head==8 && submissions==8 && max_gap==50);
    enqueue(V_DELIVERY_CRITICAL,7,"{}");
    enqueue(V_DELIVERY_BOTH,7,"{}");
    send_outbound();assert(submissions==12 && first_eid==1 && event_sequence==1 && max_gap==50);
    unsigned previous=submissions;
    enqueue(V_DELIVERY_CRITICAL,6,"{}");
    send_outbound();assert(submissions==previous); /* Stale owner is discarded. */
    release_after_send=true;enqueue(V_DELIVERY_CRITICAL,7,"{}");
    send_outbound();assert(submissions==previous);release_after_send=false;
    enqueue(V_DELIVERY_CRITICAL,7,"invalid");
    send_outbound();assert(submissions==previous);
    fail_eid=true;enqueue(V_DELIVERY_CRITICAL,7,"{}");
    send_outbound();assert(submissions==previous && head==tail && !locked);
    receiver_acknowledged=false;
    for(unsigned i=0;i<6;i++)enqueue(V_DELIVERY_PRIMARY,7,"{}");
    unsigned last_service=serviced,last_maintenance=maintained;
    assert(!send_outbound() && serviced==last_service && maintained>last_maintenance);
    assert(send_outbound() && serviced==now); /* Older peers see all control, including start, before audio. */
    puts("Production control dispatcher: bounded batches, voice fairness, shared mirror identity, stale owner and allocation faults PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='vibe-outbound-') as directory:
    target=Path(directory)
    (target/'main.c').write_text(preamble+dispatcher+main)
    subprocess.run([os.getenv('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(ROOT/'tests/vibe/stubs'),
                    '-I'+str(ROOT/'main/vibe'),str(target/'main.c'),'-o',str(target/'test')],check=True)
    subprocess.run([str(target/'test')],check=True)
