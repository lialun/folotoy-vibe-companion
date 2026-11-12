#include "hosts.h"
#include "ble_frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *ids[]={"11111111111111111111111111111111","22222222222222222222222222222222"};
static const char *tokens[]={"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
static vibe_route_t wifi(unsigned n) { return (vibe_route_t){.kind=V_LINK_WIFI,.address=n,.port=40000}; }
static vibe_route_t ble(unsigned n) { return (vibe_route_t){.kind=V_LINK_BLE,.handle=n,.serial=1}; }
static uint32_t random_state=0x51a7f00d;
static unsigned pick(unsigned n) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state%n; }
static void adversarial_sequence(void) {
    vibe_hosts_t h;vibe_hosts_init(&h,NULL,NULL);
    for (unsigned i=0;i<VIBE_HOSTS_MAX;i++) {
        char id[33],token[65];snprintf(id,sizeof(id),"%032x",i+10);snprintf(token,sizeof(token),"%064x",i+10);
        assert(vibe_hosts_register(&h.saved,id,"Identical display name",token,-1)==(int)i);
    }
    uint64_t now=1;
    for (unsigned step=0;step<100000;step++) {
        now+=pick(2000);vibe_hosts_poll(&h,now);
        unsigned host=pick(VIBE_HOSTS_MAX),kind=pick(2),op=pick(4);
        vibe_host_link_t before=vibe_hosts_active(&h,now);int owner=h.owner;vibe_link_t selected=h.selected;
        uint32_t lease=h.lease;
        vibe_route_t route=kind?ble(host+1):wifi(host+1);
        route.serial=step/100+1;route.port=(uint16_t)(40000+step%17);
        if (op==0) vibe_hosts_seen(&h,(int)host,route,now,30000,true,true);
        else if (op==1) vibe_hosts_lost(&h,route);
        else if (op==2) (void)vibe_hosts_claim(&h,(int)host,route,now);
        vibe_hosts_poll(&h,now);
        vibe_host_link_t active=vibe_hosts_active(&h,now);
        if (active.route.kind) {
            assert(h.owner>=0 && (unsigned)h.owner<h.saved.count && active.route.kind==h.selected);
            for (unsigned i=0;i<h.saved.count;i++) for (unsigned j=0;j<2;j++) {
                bool controls=vibe_hosts_controls(&h,(int)i,h.links[i][j].route,now);
                assert(controls==((int)i==h.owner && vibe_route_equal(h.links[i][j].route,active.route)));
            }
        } else assert(h.owner==-1 && h.selected==V_LINK_NONE);
        if (op==0 && before.route.kind && (int)host!=owner) {
            assert(h.owner==owner && h.selected==selected && h.lease==lease); /* Discovery cannot steal a live owner. */
        }
        if (h.owner>=0) {
            vibe_host_link_t l=vibe_hosts_active(&h,now);lease=h.lease;
            vibe_hosts_seen(&h,h.owner,l.route,now,30000,true,true);
            assert(h.lease==lease); /* Repeated heartbeats never oscillate a healthy route. */
        }
    }
}
int main(void) {
    vibe_hosts_t h;vibe_hosts_init(&h,NULL,tokens[0]);
    assert(h.saved.count==1 && vibe_hosts_auth(&h,ids[0],tokens[0])==0);
    assert(vibe_hosts_register(&h.saved,ids[0],"Same name",tokens[0],0)==0);
    assert(vibe_hosts_auth(&h,ids[1],tokens[0])==-1); /* ID cannot steal a named legacy slot. */
    assert(vibe_hosts_register(&h.saved,ids[1],"Same name",tokens[1],-1)==1);
    vibe_hosts_saved_t decoded;
    assert(vibe_hosts_decode(&decoded,&h.saved,sizeof(h.saved)));
    assert(!vibe_hosts_decode(&decoded,&h.saved,sizeof(h.saved)-1));
    vibe_hosts_seen(&h,0,wifi(1),100,30000,true,true);
    uint32_t lease=h.lease;
    vibe_hosts_seen(&h,0,ble(1),110,30000,true,true);
    vibe_hosts_seen(&h,1,ble(2),120,30000,true,true);
    assert(h.owner==0 && h.selected==V_LINK_WIFI && h.lease==lease);
    assert(!vibe_hosts_controls(&h,1,ble(2),120));
    assert(!vibe_hosts_controls(&h,0,ble(1),120)); /* Standby never duplicates Enter. */
    for (uint64_t t=200;t<20000;t+=100) {
        vibe_hosts_seen(&h,1,ble(2),t,30000,true,true);
        assert(h.owner==0 && h.selected==V_LINK_WIFI);
    }
    vibe_hosts_lost(&h,wifi(1));vibe_hosts_poll(&h,20000);
    assert(h.owner==0 && h.selected==V_LINK_BLE); /* Same computer first. */
    lease=h.lease;
    vibe_hosts_seen(&h,0,wifi(2),20001,30000,true,true);
    assert(h.selected==V_LINK_BLE && h.lease==lease); /* No healthy-route oscillation. */
    assert(!vibe_hosts_claim(&h,1,ble(1),20002)); /* A's connection cannot claim B. */
    assert(vibe_hosts_claim(&h,1,ble(2),20002));
    assert(h.owner==1 && !vibe_hosts_controls(&h,0,wifi(2),20002));
    lease=h.lease;assert(vibe_hosts_claim(&h,1,ble(2),20003));assert(h.lease==lease);
    vibe_route_t reused=ble(2);reused.serial++;
    vibe_hosts_seen(&h,1,reused,20004,30000,true,true);
    vibe_hosts_lost(&h,ble(2)); /* Late disconnect of reused HCI handle is harmless. */
    assert(vibe_hosts_controls(&h,1,reused,20004) && h.lease!=lease);
    vibe_hosts_disable(&h,V_LINK_BLE);vibe_hosts_poll(&h,20005);
    assert(h.owner==0 && h.selected==V_LINK_WIFI);
    vibe_hosts_poll(&h,60000);assert(h.owner==-1 && h.selected==V_LINK_NONE);
    for (unsigned i=h.saved.count;i<VIBE_HOSTS_MAX;i++) {
        char id[33],token[65];snprintf(id,sizeof(id),"%032x",i+10);snprintf(token,sizeof(token),"%064x",i+10);
        assert(vibe_hosts_register(&h.saved,id,"Computer",token,-1)==(int)i);
    }
    assert(vibe_hosts_register(&h.saved,"99999999999999999999999999999999","Computer",tokens[0],-1)<0);
    /* Recover a lost credential with a verified physical pairing code, without
     * consuming a ninth slot or removing the other seven computers. */
    const char *replacement="cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    assert(vibe_hosts_pair(&h.saved,ids[0],"Renamed computer",replacement)==0 && h.saved.count==VIBE_HOSTS_MAX);
    assert(vibe_hosts_auth(&h,ids[0],tokens[0])<0 && vibe_hosts_auth(&h,ids[0],replacement)==0);
    assert(vibe_hosts_auth(&h,ids[1],tokens[1])==1 && vibe_hosts_decode(&decoded,&h.saved,sizeof(h.saved)));
    assert(vibe_hosts_pair(&h.saved,ids[0],"Computer",tokens[1])<0); /* Never alias two identities. */
    vibe_hosts_t legacy;vibe_hosts_init(&legacy,NULL,tokens[0]);
    assert(vibe_hosts_register(&legacy.saved,ids[1],"Computer",tokens[1],-1)==1);
    assert(vibe_hosts_register(&legacy.saved,ids[1],"Computer",tokens[0],0)<0);
    assert(vibe_hosts_decode(&decoded,&legacy.saved,sizeof(legacy.saved))); /* Migration cannot create duplicate IDs. */
    adversarial_sequence();
    /* MTU 23, 185, 247; every byte of a control packet must round-trip. */
    for (unsigned cap=20;cap<=244;cap+=41) {
        vibe_ble_frame_t f={0};uint8_t input[1537],frame[244];
        for (unsigned i=0;i<sizeof(input);i++) input[i]=(uint8_t)i;
        for (unsigned off=0;off<sizeof(input);) {
            unsigned chunk=sizeof(input)-off;if (chunk>cap-6) chunk=cap-6;
            vibe_ble_frame_header(frame,17,off,sizeof(input));memcpy(frame+6,input+off,chunk);
            assert(vibe_ble_frame_add(&f,frame,chunk+6,10)==(off+chunk==sizeof(input)?1:0));off+=chunk;
        }
        assert(!memcmp(input,f.bytes,sizeof(input)));vibe_ble_frame_clear(&f);
        vibe_ble_frame_header(frame,1,0,10);memset(frame+6,1,2);assert(vibe_ble_frame_add(&f,frame,8,1)==0);
        vibe_ble_frame_header(frame,1,3,10);assert(vibe_ble_frame_add(&f,frame,8,1)<0 && !f.bytes);
        vibe_ble_frame_header(frame,1,0,10);assert(vibe_ble_frame_add(&f,frame,8,1)==0);
        vibe_ble_frame_header(frame,1,2,10);assert(vibe_ble_frame_add(&f,frame,8,2002)<0 && !f.bytes);
        for (unsigned n=0;n<sizeof(frame);n++) {
            memset(frame,255,sizeof(frame));(void)vibe_ble_frame_add(&f,frame,n,1);vibe_ble_frame_clear(&f);
        }
    }
    puts("Host ownership, migration, route faults and BLE framing: PASS");
}
