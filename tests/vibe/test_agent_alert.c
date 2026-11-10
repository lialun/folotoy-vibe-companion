#include "agent_alert.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    vibe_agent_alert_t s={0};
    assert(sizeof(s)<=320);
    assert(!vibe_agent_push(&s,0,0,0,0,10000));
    assert(!vibe_agent_push(&s,1,7,0,0,10000));
    assert(!vibe_agent_push(&s,1,0,4,0,10000));
    assert(vibe_agent_push(&s,1,0,0,0,10000));
    assert(vibe_agent_push(&s,1,0,0,1,10000) && s.count==1);
    assert(!vibe_agent_tick(&s,500,true));
    assert(vibe_agent_tick(&s,1000,false) && s.visible && s.active.id==1);
    assert(!vibe_agent_tick(&s,1100,false));
    assert(vibe_agent_push(&s,2,1,1,1000,10000));
    assert(!vibe_agent_tick(&s,11000,false) && !s.visible && s.count==0);
    for(unsigned i=0;i<100;i++) assert(vibe_agent_push(&s,3+i,i%7,i%4,12000,10000));
    assert(s.count==4);
    assert(vibe_agent_tick(&s,13000,false));
    assert(!vibe_agent_tick(&s,24000,false));
    vibe_agent_alert_t burst={0};
    assert(vibe_agent_push(&burst,1,0,0,0,10000));assert(vibe_agent_tick(&burst,0,false));
    assert(vibe_agent_push(&burst,2,1,1,100,10000));assert(!vibe_agent_tick(&burst,1000,false));
    assert(vibe_agent_tick(&burst,2000,false) && burst.active.id==2);
    vibe_agent_alert_t popup={0};
    assert(vibe_agent_push(&popup,101,0,0,0,10000));
    assert(vibe_agent_tick(&popup,0,false) && popup.visible_until==5000);
    assert(!vibe_agent_tick(&popup,4999,false) && popup.visible);
    assert(!vibe_agent_tick(&popup,5000,false) && !popup.visible);
    assert(vibe_agent_push(&popup,102,1,1,6000,10000));
    assert(vibe_agent_tick(&popup,6000,false));
    assert(vibe_agent_push(&popup,103,2,2,6100,10000));
    assert(vibe_agent_dismiss(&popup) && !popup.visible && !popup.count);
    assert(!vibe_agent_tick(&popup,6200,false));
    assert(!vibe_agent_dismiss(&popup));
    /* A retry of a dismissed or already queued notice must not re-open it. */
    assert(vibe_agent_push(&popup,102,1,1,6300,10000));
    assert(vibe_agent_push(&popup,103,2,2,6300,10000));
    assert(!popup.count && !vibe_agent_tick(&popup,6300,false));
    assert(vibe_agent_push(&popup,104,0,0,6400,10000));
    assert(vibe_agent_tick(&popup,6400,false) && popup.active.id==104);
    vibe_agent_alert_t deferred={0};
    assert(vibe_agent_push(&deferred,105,0,0,0,10000));
    assert(!vibe_agent_dismiss(&deferred) && deferred.count==1);
    assert(!vibe_agent_tick(&deferred,10000,true) && !deferred.count);
    assert(!strcmp(vibe_agent_name(3),"DSH"));
    assert(vibe_agent_hash("a")==vibe_agent_hash("a") && vibe_agent_hash("a")!=vibe_agent_hash("b"));
    printf("Agent fixed queue: PASS (%zu resident bytes)\n",sizeof(s));
}
