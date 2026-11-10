/* Exercise ownership when producer buffers change, queues fill, or RAM runs out. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "messages.h"
static unsigned allocations;
static bool fail_allocate,full;
static vibe_msg_t queued;
static void *allocate(size_t size) {
    if (fail_allocate) return NULL;
    void *p=malloc(size);if (p) ++allocations;return p;
}
static void release(void *p) { if (p) { assert(allocations);--allocations;free(p); } }
#define malloc allocate
#define free release
#include "../../main/vibe/messages.c"
#undef malloc
#undef free
int xQueueSend(QueueHandle_t queue,const void *message,unsigned timeout) {
    assert(queue==(void *)1 && timeout==0);
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
    puts("Compact message queue: ownership, bounds, full queue and allocation failure PASS");
}
