#include "ble_links.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    vibe_ble_parameters_t p;vibe_ble_parameters_start(&p,100);
    assert(!vibe_ble_parameters_due(&p,1099,24,36));assert(vibe_ble_parameters_due(&p,1100,24,36));assert(p.waiting && p.attempts==1);
    /* A 0 command return does not imply success: actual LL collision leaves
     * the Mac's initial 360 ms timeout unchanged. Retry after a settle period. */
    vibe_ble_parameters_observe(&p,1150,0,36);assert(!p.waiting && p.due==2150);
    assert(!vibe_ble_parameters_due(&p,2149,24,36));assert(vibe_ble_parameters_due(&p,2150,24,36));
    vibe_ble_parameters_observe(&p,2200,0,600);assert(!p.due && !p.waiting);
    assert(!vibe_ble_parameters_due(&p,999999,24,36));
    /* Central later chooses a short timeout: use remaining bounded attempts. */
    vibe_ble_parameters_observe(&p,3000,0,36);assert(vibe_ble_parameters_due(&p,5000,24,36));
    vibe_ble_parameters_observe(&p,5010,0,36);assert(vibe_ble_parameters_due(&p,9010,24,36));
    vibe_ble_parameters_observe(&p,9020,0,36);assert(!p.due && p.attempts==4);
    assert(!vibe_ble_parameters_due(&p,999999,24,36));
    /* A lost completion event also cannot keep an idle periodic timer alive. */
    vibe_ble_parameters_start(&p,100);
    for(unsigned i=0;i<4;i++){assert(vibe_ble_parameters_due(&p,1100+i*VIBE_BLE_PARAMETER_WAIT_MS,24,36));assert(p.attempts==i+1);}
    assert(!vibe_ble_parameters_due(&p,1100+4*VIBE_BLE_PARAMETER_WAIT_MS,24,36) && !p.due);
    vibe_ble_links_t links;vibe_ble_links_init(&links);
    vibe_ble_link_t *a=vibe_ble_link_ensure(&links,1),*b=vibe_ble_link_ensure(&links,2);assert(a && b);
    vibe_ble_parameters_start(&a->parameters,0);vibe_ble_parameters_start(&b->parameters,500);
    assert(vibe_ble_parameters_due(&a->parameters,1000,24,36) && !vibe_ble_parameters_due(&b->parameters,1000,24,36));
    vibe_ble_link_remove(&links,1);a=vibe_ble_link_ensure(&links,1);assert(!a->parameters.due && !a->parameters.attempts);
    assert(b->parameters.due==1500);
    vibe_ble_links_init(&links);assert(!links.links[0].parameters.due && !links.links[1].parameters.due);
    vibe_ble_parameters_start(&p,0);
    assert(!vibe_ble_parameters_due(&p,1000,24,600) && !p.due);
    vibe_ble_parameters_stream(&p,1100,true);
    assert(!vibe_ble_parameters_due(&p,1100,24,600));
    assert(vibe_ble_parameters_due(&p,1101,24,600) && p.waiting && p.requested_streaming);
    assert(!vibe_ble_parameters_due(&p,1200,24,600) && p.waiting);
    vibe_ble_parameters_stream(&p,1200,false);assert(p.waiting && p.due==6101);
    vibe_ble_parameters_observe(&p,1220,0,600);
    assert(!p.waiting && p.due==2220 && p.relax);
    assert(vibe_ble_parameters_due(&p,2220,12,600) && !p.requested_streaming);
    vibe_ble_parameters_observe(&p,2250,0,600);
    assert(!p.due && !p.relax && !vibe_ble_parameters_due(&p,999999,24,600));

    /* Short voice/radio transitions keep the already-fast link and cancel
     * the delayed relaxation, without another connection procedure. */
    vibe_ble_parameters_stream(&p,3000,true);
    assert(vibe_ble_parameters_due(&p,3001,24,600));
    vibe_ble_parameters_observe(&p,3050,0,600);
    vibe_ble_parameters_stream(&p,3100,false);
    vibe_ble_parameters_stream(&p,3500,true);
    assert(!vibe_ble_parameters_due(&p,3501,12,600) && !p.due);

    /* A central may validly retain 30 ms. Accept that completed choice,
     * while a synchronous busy error still gets bounded retry. */
    vibe_ble_parameters_stream(&p,4000,false);
    assert(!vibe_ble_parameters_due(&p,5000,24,600));
    vibe_ble_parameters_stream(&p,5100,true);
    assert(vibe_ble_parameters_due(&p,5101,24,600));
    vibe_ble_parameters_observe(&p,5150,0,600);
    assert(!p.due && !vibe_ble_parameters_due(&p,999999,24,600));
    vibe_ble_parameters_stream(&p,6000,false);
    assert(vibe_ble_parameters_due(&p,7000,12,600));
    vibe_ble_parameters_observe(&p,7001,7,600);
    assert(p.relax && p.due==8001);
    vibe_ble_parameters_observe(&p,7050,0,600);assert(p.due==8001 && p.relax);
    assert(vibe_ble_parameters_due(&p,8001,12,600));
    vibe_ble_parameters_observe(&p,8050,0,600);assert(!p.due && !p.relax);

    /* A late completion cannot defeat the initial encryption/PHY settle. */
    vibe_ble_parameters_start(&p,100);
    vibe_ble_parameters_stream(&p,110,true);
    vibe_ble_parameters_observe(&p,150,0,600);
    assert(p.due==1100 && !vibe_ble_parameters_due(&p,1099,24,600));
    assert(vibe_ble_parameters_due(&p,1100,24,600));
    vibe_ble_parameters_stream(&p,1110,false);
    vibe_ble_parameters_stream(&p,1120,true);
    vibe_ble_parameters_observe(&p,1150,0,600);assert(!p.due);
    puts("BLE parameters: deferred LL procedures, actual supervision check, collision/backoff, bounded missing completion, independent links and handle reuse PASS");
}
