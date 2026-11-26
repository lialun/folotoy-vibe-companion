#include "input_gate.h"
#include "connection_request.h"
#include <assert.h>
#include <stdio.h>

static const char *first="11111111-1111-1111-1111-111111111111";
static const char *second="22222222-2222-2222-2222-222222222222";
static vibe_input_result_t press(vibe_prompt_input_t *g,vibe_key_t key,bool lit,uint32_t frame) {
    return vibe_prompt_input_event(g,key,V_PRESS,lit,frame);
}
static vibe_input_result_t release(vibe_prompt_input_t *g,vibe_key_t key,bool lit,uint32_t frame) {
    return vibe_prompt_input_event(g,key,V_RELEASE,lit,frame);
}
int main(void) {
    vibe_enter_intent_t intent={0};
    for (unsigned i=0;i<1000;i++) {
        uint64_t now=100+i*1000;
        assert(!vibe_enter_intent_event(&intent,V_UP,V_PRESS,now));
        assert(!vibe_enter_intent_event(&intent,V_UP,V_RELEASE,now+100));
        assert(!vibe_enter_intent_event(&intent,V_UP,V_RELEASE,now+110));
        assert(intent.command==i);
        assert(!vibe_enter_intent_event(&intent,V_DOWN,V_PRESS,now+120));
        assert(vibe_enter_intent_event(&intent,V_DOWN,V_RELEASE,now+140));
        assert(intent.command==i+1 && intent.down_at==now+120 && intent.hold_ms==20);
        assert(!vibe_enter_intent_event(&intent,V_DOWN,V_RELEASE,now+150));
    }
    vibe_enter_intent_event(&intent,V_UP,V_PRESS,2000000);
    vibe_enter_intent_event(&intent,V_DOWN,V_PRESS,2000010);
    vibe_enter_intent_event(&intent,V_UP,V_RELEASE,2000100);
    assert(!vibe_enter_intent_event(&intent,V_DOWN,V_RELEASE,2000200));
    vibe_enter_intent_event(&intent,V_DOWN,V_PRESS,2000300);
    vibe_enter_intent_event(&intent,V_UP,V_PRESS,2000310);
    vibe_enter_intent_event(&intent,V_UP,V_RELEASE,2000400);
    assert(!vibe_enter_intent_event(&intent,V_DOWN,V_RELEASE,2000500));
    vibe_enter_intent_event(&intent,V_DOWN,V_PRESS,2000600);
    vibe_enter_intent_event(&intent,V_DOWN,V_PRESS,2000601);
    assert(!vibe_enter_intent_event(&intent,V_DOWN,V_RELEASE,2000650));
    uint32_t command=intent.command;
    vibe_enter_intent_reset(&intent);
    assert(intent.command==command && !intent.down && !intent.up);
    vibe_enter_intent_event(&intent,V_DOWN,V_PRESS,2000700);
    assert(vibe_enter_intent_event(&intent,V_DOWN,V_RELEASE,2000750) && intent.command==command+1);
    vibe_prompt_input_t g={0};
    vibe_prompt_input_sync(&g,true,first,0);
    assert(press(&g,V_DOWN,false,0)==V_INPUT_CONSUMED);
    vibe_prompt_input_sync(&g,true,first,10); /* Wake frame arrives while held. */
    assert(vibe_prompt_input_event(&g,V_DOWN,V_LONG,true,10)==V_INPUT_CONSUMED);
    assert(release(&g,V_DOWN,true,10)==V_INPUT_CONSUMED);
    assert(press(&g,V_UP,true,10)==V_INPUT_CONSUMED);
    assert(release(&g,V_UP,true,10)==V_INPUT_ACCEPT);

    g=(vibe_prompt_input_t){0};vibe_prompt_input_sync(&g,true,first,10);
    assert(press(&g,V_DOWN,false,10)==V_INPUT_CONSUMED); /* Panel still dark. */
    assert(release(&g,V_DOWN,true,10)==V_INPUT_CONSUMED);
    assert(press(&g,V_DOWN,true,10)==V_INPUT_CONSUMED);
    assert(release(&g,V_DOWN,true,10)==V_INPUT_REJECT);

    g=(vibe_prompt_input_t){0};vibe_prompt_input_sync(&g,true,first,10);
    assert(press(&g,V_UP,true,0)==V_INPUT_CONSUMED); /* Queued before rendering. */
    assert(release(&g,V_UP,true,10)==V_INPUT_CONSUMED);
    vibe_prompt_input_sync(&g,true,second,11);
    assert(press(&g,V_UP,true,10)==V_INPUT_CONSUMED); /* Previous request's frame. */
    assert(release(&g,V_UP,true,11)==V_INPUT_CONSUMED);
    assert(press(&g,V_UP,true,11)==V_INPUT_CONSUMED);
    vibe_prompt_input_sync(&g,false,second,0); /* Cancel/expire while held. */
    assert(release(&g,V_UP,true,0)==V_INPUT_CONSUMED);
    assert(!g.down && !g.consumed && !g.candidate);

    g=(vibe_prompt_input_t){0};assert(press(&g,V_DOWN,true,0)==V_INPUT_PASS);
    vibe_prompt_input_sync(&g,true,first,12); /* Request arrives during a Home key. */
    assert(release(&g,V_DOWN,true,12)==V_INPUT_CONSUMED);
    assert(press(&g,V_UP,true,12)==V_INPUT_CONSUMED);
    assert(press(&g,V_DOWN,true,12)==V_INPUT_CONSUMED); /* Chord cancels eligibility. */
    assert(release(&g,V_UP,true,12)==V_INPUT_CONSUMED);
    assert(release(&g,V_DOWN,true,12)==V_INPUT_CONSUMED);
    assert(press(&g,V_SETTINGS,true,12)==V_INPUT_CONSUMED);
    assert(release(&g,V_SETTINGS,true,12)==V_INPUT_REJECT);

    g=(vibe_prompt_input_t){0};vibe_prompt_input_sync(&g,true,first,12);
    press(&g,V_UP,true,12);vibe_prompt_input_sync(&g,true,first,0);
    assert(release(&g,V_UP,true,0)==V_INPUT_CONSUMED);
    assert(press(&g,(vibe_key_t)-1,true,12)==V_INPUT_CONSUMED);
    for (unsigned i=1;i<=1000;i++) {
        vibe_prompt_input_sync(&g,false,first,0);vibe_prompt_input_sync(&g,true,first,i);
        assert(press(&g,V_DOWN,false,0)==V_INPUT_CONSUMED);
        assert(release(&g,V_DOWN,true,i)==V_INPUT_CONSUMED);
        assert(press(&g,V_UP,true,i)==V_INPUT_CONSUMED);
        assert(release(&g,V_UP,true,i)==V_INPUT_ACCEPT);
        assert(!g.down && !g.consumed && !g.candidate);
    }
    puts("Input gate: dark/queued wake, exact rendered request, held-key handoff, chords, expiry and 1000 cycles PASS");
}
