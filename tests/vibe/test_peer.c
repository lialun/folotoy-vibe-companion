#include "peer.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(vibe_peer_use_wifi(10000,9999,9999,true,true,false));
    assert(!vibe_peer_use_wifi(10000,9999,9999,true,true,true));
    assert(vibe_peer_use_wifi(10000,9999,4000,true,true,true));
    assert(!vibe_peer_use_wifi(10000,4000,9999,true,true,false));
    assert(vibe_peer_use_wifi(10000,4001,9999,true,true,false));
    /* Short jitter cannot make the device override the computer's sticky route. */
    assert(!vibe_peer_use_wifi(10000,9999,5000,true,true,true));
    assert(vibe_peer_use_wifi(10000,5000,9999,true,true,false));
    assert(vibe_peer_use_wifi(10000,9999,9999,true,false,true));
    assert(!vibe_peer_use_wifi(10000,9999,9999,false,true,false));
    assert(!vibe_peer_use_wifi(10000,10001,9999,true,true,false));
    const uint32_t computer=0x0101a8c0,other=0x0201a8c0;
    assert(vibe_peer_matches(computer,33333,computer,33333)); /* Legacy client. */
    assert(vibe_peer_matches(computer,52123,computer,52123));
    assert(!vibe_peer_matches(computer,52123,computer,33333));
    assert(!vibe_peer_matches(computer,52123,other,52123));
    assert(!vibe_peer_matches(0,0,computer,52123));
    /* The replacement owner has a new source port: delayed old commands fail. */
    assert(!vibe_peer_matches(computer,52124,computer,52123));
    assert(vibe_peer_matches(computer,52124,computer,52124));
    /* Audio/replies refresh sent, so they replace an otherwise due ping. */
    assert(!vibe_peer_heartbeat_due(6000,1000,1000,true,5000));
    assert(!vibe_peer_heartbeat_due(7999,1000,1000,true,5000));
    assert(vibe_peer_heartbeat_due(8000,1000,1000,true,5000));
    assert(!vibe_peer_heartbeat_due(6000,5900,1000,true,5000));
    assert(!vibe_peer_heartbeat_due(6000,6000,6000,true,5000));
    assert(!vibe_peer_heartbeat_due(6000,0,0,true,5000));
    assert(!vibe_peer_heartbeat_due(6000,0,1000,false,5000));
    assert(!vibe_peer_heartbeat_due(16000,1000,1000,true,5000));
    assert(!vibe_peer_heartbeat_due(1000,2000,1000,true,5000));
    assert(!vibe_peer_heartbeat_due(1000,0,2000,true,5000));
    assert(vibe_peer_interval(30)==30000 && vibe_peer_timeout(30000)==90000);
    assert(vibe_peer_interval(10)==10000 && vibe_peer_timeout(10000)==30000);
    assert(!vibe_peer_heartbeat_due(12999,1000,1000,true,10000));
    assert(vibe_peer_heartbeat_due(13000,1000,1000,true,10000));
    assert(!vibe_peer_heartbeat_due(31000,1000,1000,true,10000));
    assert(!vibe_peer_heartbeat_due(13000,12999,1000,true,10000));
    assert(vibe_peer_interval(0)==5000 && vibe_peer_interval(31)==5000);
    assert(!vibe_peer_heartbeat_due(32999,1000,1000,true,30000));
    assert(vibe_peer_heartbeat_due(33000,1000,1000,true,30000));
    assert(!vibe_peer_heartbeat_due(91000,1000,1000,true,30000));
    assert(!vibe_peer_heartbeat_due(33000,32999,1000,true,30000));
    assert(!vibe_peer_claim_allowed(false,false,false,false)); /* Discovery cannot claim. */
    assert(!vibe_peer_claim_allowed(false,false,true,true)); /* Nor renew an owner. */
    assert(vibe_peer_claim_allowed(false,true,false,false)); /* Physical pairing. */
    assert(!vibe_peer_claim_allowed(false,true,true,false)); /* Two computers cannot race. */
    assert(vibe_peer_claim_allowed(true,false,true,true));
    assert(!vibe_peer_claim_allowed(true,false,true,false));
    assert(vibe_peer_claim_allowed(true,false,false,false)); /* Expired lease. */
    puts("LAN peer policy: dynamic/legacy ports, stale endpoint rejection, traffic-aware heartbeat PASS");
}
