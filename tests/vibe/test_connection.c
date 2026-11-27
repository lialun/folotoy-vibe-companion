#include "peer.h"
#include "radio/stream.h"
#include "connection_request.h"
#include "trusted_computers.h"
#include "voice_tx.h"
#include "cJSON.h"
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define portMAX_DELAY 0
#define VIBE_JSON_MAX 1536
#define ESP_LOGI(...) ((void)0)
static int v_mutex=1,peer_mutex=2,v_queue=1,udp=1;
static struct sockaddr_in peer,ble_peer,commit_peer;
static bool ble_owner,commit_ble,prefer_ble;
static uint64_t owner_seen,wifi_seen,ble_seen,now_ms=10000;
static uint32_t owner_generation=1;
static uint32_t release_generation;
static bool release_after_send;
static uint64_t last_sent_ms;
static char owner_client[37],posted[1537],reply[256];
static unsigned post_count,hello_count;
static uint32_t posted_generation;
static unsigned repair_count;
static unsigned feedback_count;

static uint32_t last_repair_id;
static void vibe_voice_uplink_ready(uint64_t session,uint32_t owner) {(void)session;assert(owner==owner_generation);}
static void vibe_voice_uplink_cancel(uint64_t session,uint32_t owner) {(void)session;assert(owner==owner_generation);}
static bool vibe_voice_repair_request(uint64_t session,const uint32_t *frames,unsigned count,uint32_t owner,uint32_t request_id) {
    last_repair_id=request_id;
    if(!session)return false;
    assert(frames && count && count<=4 && owner==owner_generation);++repair_count;return true;
}
static bool hello_authorized,hello_checked,hello_trusted;
static vibe_trusted_computers_t v_computers;
static bool v_legacy_computer;
static unsigned char v_quotas[8];
static struct { bool ble_peer,provisioning,voice_feedback,peer,audio_ready,hotkey_ready,radio_pcm,voice_v2,voice_repair,voice_handshake,voice_interleave;unsigned heartbeat_ms;uint64_t peer_seen_ms;char token[65];vibe_connection_request_t connection_request; } v_status;
static struct { bool recording; } v_model;
typedef struct { unsigned heartbeat;bool paired;char device[18]; } link_snapshot_t;
static bool locks[3];
static void xSemaphoreTake(int lock,int wait) { (void)wait;assert(!locks[lock]);assert(!(lock==1 && locks[2]));locks[lock]=true; }
static void xSemaphoreGive(int lock) { assert(locks[lock]);locks[lock]=false; }
static uint64_t vibe_now(void) { return now_ms; }
static link_snapshot_t link_snapshot(void) { return (link_snapshot_t){.heartbeat=5000,.paired=v_computers.count>0 || v_legacy_computer,.device="aabbccddeeff"}; }
static size_t copy_text(char *to,const char *from,size_t cap) { size_t n=strlen(from);snprintf(to,cap,"%s",from);return n; }
#define strlcpy copy_text
static bool vibe_json_depth_ok(const char *text,size_t n) { (void)text;return n>0; }
static bool vibe_message_post_link_json(int queue,const char *text,size_t n,uint32_t generation) {
    (void)queue;assert(n<sizeof(posted));memcpy(posted,text,n);posted[n]=0;posted_generation=generation;++post_count;return true;
}
static void hello_to(const struct sockaddr_in *to,bool ble,bool auth,bool checked,bool trusted) {
    hello_trusted=trusted;
    hello_checked=checked;
    (void)to;(void)ble;(void)checked;hello_authorized=auth;++hello_count;
}
/* Cryptographic hash is supplied by mbedTLS on target; this mock tests approval gating, not SHA-256. */
static bool vibe_connection_approved(const char *token,const char *client,const char *request) {
    vibe_connect_tick(&v_status.connection_request,now_ms);
    return v_status.connection_request.phase==V_CONNECT_ACCEPTED && vibe_connect_matches(&v_status.connection_request,client,request,token);
}
typedef struct { int type; } vibe_msg_t;
enum { V_MSG_CONNECTION_CHANGED };
static int xQueueSend(int queue,const vibe_msg_t *message,int wait) { (void)queue;(void)message;(void)wait;return 1; }
static int vibe_ble_send_to(uint16_t handle,unsigned char type,const void *bytes,size_t n) {
    (void)handle;(void)type;assert(n<sizeof(reply));memcpy(reply,bytes,n);reply[n]=0;return 0;
}
static ssize_t capture_send(int fd,const void *bytes,size_t n,int flags,const struct sockaddr *from,socklen_t size) {
    (void)fd;(void)flags;(void)from;(void)size;assert(n-1<sizeof(reply));memcpy(reply,(const char *)bytes+1,n-1);reply[n-1]=0;return (ssize_t)n;
}
static unsigned media_count;
void vibe_radio_remote_receive(const uint8_t *packet,size_t n,uint32_t generation,bool ble) {
    (void)packet;(void)ble;assert(n==V_RADIO_PACKET && generation==owner_generation && !locks[1] && !locks[2]);++media_count;
}
static bool vibe_voice_uplink_feedback(const vibe_voice_feedback_t *feedback,uint32_t owner) {
    assert(feedback->session==1 && feedback->id==1 && owner==owner_generation && !locks[1] && !locks[2]);
    ++feedback_count;return true;
}
#define sendto capture_send
#include "link_owner.inc"
#include "link_receive.inc"
#include "link_release.inc"
static struct sockaddr_in endpoint(unsigned host,unsigned port) {
    struct sockaddr_in from={.sin_family=AF_INET,.sin_addr.s_addr=htonl(host),.sin_port=htons(port)};return from;
}
static void receive(unsigned type,const char *json,struct sockaddr_in from,bool ble) {
    unsigned char packet[1537];size_t n=strlen(json);assert(n<sizeof(packet));packet[0]=type;memcpy(packet+1,json,n);process_packet(packet,n+1,&from,ble);
}
static void reset(bool bound) {
    memset(&peer,0,sizeof(peer));memset(&ble_peer,0,sizeof(ble_peer));memset(&v_status,0,sizeof(v_status));
    if (bound) memset(v_status.token,'a',64);
    v_computers=(vibe_trusted_computers_t){0};v_legacy_computer=bound;
    ble_owner=false;prefer_ble=false;owner_seen=wifi_seen=ble_seen=0;owner_generation=1;owner_client[0]=0;post_count=hello_count=0;v_model.recording=false;
    release_generation=0;release_after_send=false;last_sent_ms=0;
}
int main(void) {
    struct sockaddr_in a=endpoint(0xc0a80102,51001),b=endpoint(0xc0a80103,51002),ble=endpoint(0x7f000001,1);
    const char *client="11111111-1111-1111-1111-111111111111",*request="22222222-2222-2222-2222-222222222222";
    const char *credential="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    char auth[400],pair[400],ask[512];
    snprintf(auth,sizeof(auth),"{\"role\":\"pc\",\"type\":\"pc.status\",\"client\":\"%s\",\"device\":\"aabbccddeeff\",\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}",client);
    snprintf(pair,sizeof(pair),"{\"type\":\"pair\",\"client\":\"%s\",\"request\":\"%s\",\"device\":\"aabbccddeeff\",\"token\":\"%s\"}",client,request,credential);
    snprintf(ask,sizeof(ask),"{\"type\":\"connection.request\",\"client\":\"%s\",\"request\":\"%s\",\"device\":\"aabbccddeeff\",\"digest\":\"%s\",\"name\":\"Office Mac\"}",client,request,credential);
    reset(true);receive(3,"{\"role\":\"pc\"}",a,false);assert(!peer.sin_port && !post_count && hello_count==1);
    receive(3,auth,a,false);assert(hello_authorized && post_count==1 && peer.sin_port==a.sin_port);
    uint8_t media[V_RADIO_PACKET]={5};unsigned before_media=media_count;
    process_packet(media,sizeof(media),&b,false);assert(media_count==before_media);
    process_packet(media,sizeof(media),&a,false);assert(media_count==before_media+1);
    process_packet(media,sizeof(media),&ble,true);assert(media_count==before_media+1);
    uint32_t generation=owner_generation;
    receive(3,auth,ble,true);assert(hello_authorized && ble_owner && peer.sin_port==a.sin_port && owner_generation==generation);
    uint8_t feedback[V_VOICE_FEEDBACK_BYTES]={V_VOICE_FEEDBACK_TYPE,1};
    memset(feedback+2,0xaa,32);feedback[34]=1;feedback[42]=1;
    unsigned feedback_before=feedback_count,posted_before_feedback=post_count;
    v_status.heartbeat_ms=5000;
    process_packet(feedback,sizeof(feedback),&a,false);assert(feedback_count==feedback_before);
    v_status.voice_feedback=true;
    process_packet(feedback,sizeof(feedback),&b,false);assert(feedback_count==feedback_before);
    process_packet(feedback,sizeof(feedback),&a,false);assert(feedback_count==feedback_before+1);
    process_packet(feedback,sizeof(feedback),&ble,true);assert(feedback_count==feedback_before+2);
    feedback[2]^=1;process_packet(feedback,sizeof(feedback),&a,false);feedback[2]^=1;
    assert(feedback_count==feedback_before+2 && post_count==posted_before_feedback);
    release_after_send=true;process_packet(feedback,sizeof(feedback),&a,false);release_after_send=false;
    assert(feedback_count==feedback_before+2);
    uint64_t saved_now=now_ms;now_ms+=120000;process_packet(feedback,sizeof(feedback),&a,false);now_ms=saved_now;
    assert(feedback_count==feedback_before+2);
    char repair[512];unsigned posted_before_repair=post_count,repairs_before=repair_count;
    const char *repair_frames[]={"[1,2]","[]","[0.5]","[-1]","[2000]","[null]","[1,2,3,4,5]","1","[1e999]"};
    for(unsigned i=0;i<sizeof(repair_frames)/sizeof(repair_frames[0]);i++) {
        snprintf(repair,sizeof(repair),"{\"type\":\"voice.repair\",\"client\":\"%s\",\"device\":\"aabbccddeeff\",\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"voiceSession\":\"123456789abcdef0\",\"frames\":%s}",client,repair_frames[i]);
        receive(2,repair,a,false);
    }
    assert(repair_count==repairs_before+1 && post_count==posted_before_repair);
    snprintf(repair,sizeof(repair),"{\"type\":\"voice.repair\",\"client\":\"%s\",\"device\":\"aabbccddeeff\",\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"voiceSession\":\"123456789abcdef0\",\"frames\":[2]}",client);
    receive(2,repair,b,false);assert(repair_count==repairs_before+1); /* Token alone cannot replace the endpoint. */
    receive(2,repair,ble,true);assert(repair_count==repairs_before+2);
    release_after_send=true;receive(2,repair,a,false);assert(repair_count==repairs_before+2);release_after_send=false;
    memset(strstr(repair,"aaaaaaaa"),'c',64);receive(2,repair,a,false);assert(repair_count==repairs_before+2);
    unsigned saved_repairs=repair_count;
    const char *repair_ids[]={"0","-1","true","1.5","4294967296","1e999","42"};
    for(unsigned i=0;i<sizeof(repair_ids)/sizeof(repair_ids[0]);i++) {
        snprintf(repair,sizeof(repair),"{\"type\":\"voice.repair\",\"client\":\"%s\",\"device\":\"aabbccddeeff\",\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"voiceSession\":\"123456789abcdef0\",\"frames\":[2],\"repairID\":%s}",client,repair_ids[i]);
        receive(2,repair,a,false);
    }
    assert(repair_count==saved_repairs+1 && last_repair_id==42);
    process_packet(media,sizeof(media),&ble,true);assert(media_count==before_media+2);
    char other[400];strcpy(other,auth);memset(strstr(other,client),'3',8);
    receive(3,other,b,false);assert(!hello_authorized && peer.sin_port==a.sin_port && owner_generation==generation);
    receive(2,pair,b,false);assert(post_count==2); /* No physical approval, even for a bound device. */
    receive(2,ask,b,false);assert(strstr(reply,"pending") && v_status.connection_request.replacing && peer.sin_port==a.sin_port);
    uint64_t deadline=v_status.connection_request.until;
    now_ms+=1000;receive(2,ask,ble,true);assert(v_status.connection_request.until==deadline); /* A shared request across both links. */
    receive(2,pair,b,false);assert(post_count==2);
    vibe_connect_decide(&v_status.connection_request,true,now_ms);
    strcpy(other,pair);memset(strstr(other,credential),'c',64);receive(2,other,b,false);assert(post_count==2);
    receive(2,pair,b,false);assert(post_count==3 && posted_generation==0 && commit_peer.sin_port==b.sin_port && !commit_ble);
    assert(peer.sin_port==a.sin_port && owner_generation==generation); /* Persistence/commit belongs to the controller. */
    now_ms+=20001;receive(2,pair,b,false);assert(post_count==3 && v_status.connection_request.phase==V_CONNECT_EXPIRED);
    reset(false);v_model.recording=true;receive(2,ask,b,false);assert(strstr(reply,"busy") && v_status.connection_request.phase==V_CONNECT_NONE);
    v_model.recording=false;receive(2,ask,b,false);vibe_connect_decide(&v_status.connection_request,false,now_ms);
    receive(2,ask,b,false);assert(strstr(reply,"denied"));receive(2,pair,b,false);assert(!post_count);
    vibe_connection_request_t r={0};
    assert(!vibe_connect_begin(&r,"bad",request,credential,"Mac",false,1));
    assert(!vibe_connect_begin(&r,client,request,credential,"Mac\nspoof",false,1));
    assert(vibe_connect_begin(&r,client,request,credential,"Mac",false,1));
    assert(!vibe_connect_begin(&r,request,client,credential,"Other",false,2));
    vibe_connect_tick(&r,45001);assert(r.phase==V_CONNECT_EXPIRED);vibe_connect_decide(&r,true,45002);assert(r.phase==V_CONNECT_EXPIRED);
    assert(!locks[1] && !locks[2]);
    reset(true);receive(3,auth,a,false);receive(3,auth,ble,true);
    char preferred[512];snprintf(preferred,sizeof(preferred),"%.*s,\"primaryTransport\":\"bluetooth\"}",(int)strlen(auth)-1,auth);
    receive(3,preferred,ble,true);
    assert(prefer_ble && !vibe_peer_use_wifi(now_ms,wifi_seen,ble_seen,true,ble_owner,prefer_ble));
    char rejected[512];snprintf(rejected,sizeof(rejected),"%.*s,\"primaryTransport\":\"wifi\"}",(int)strlen(auth)-1,auth);
    memset(strstr(rejected,"aaaaaaaa"),'c',64);
    receive(3,rejected,b,false);assert(prefer_ble); /* Unauthenticated cannot change routing. */
    snprintf(rejected,sizeof(rejected),"%.*s,\"primaryTransport\":\"wifi\"}",(int)strlen(auth)-1,auth);
    memset(strstr(rejected,client),'3',8);
    receive(3,rejected,b,false);assert(prefer_ble); /* A different client cannot change an occupied session. */
    snprintf(preferred,sizeof(preferred),"%.*s,\"primaryTransport\":\"wifi\"}",(int)strlen(auth)-1,auth);
    receive(3,preferred,a,false);
    assert(!prefer_ble && vibe_peer_use_wifi(now_ms,wifi_seen,ble_seen,true,ble_owner,prefer_ble));
    for (unsigned cycle=0;cycle<100;cycle++) {
        reset(true);receive(3,auth,a,false);receive(3,auth,ble,true);
        uint32_t old_generation=owner_generation;unsigned before=post_count;
        v_status.token[0]=0;v_computers=(vibe_trusted_computers_t){0};v_legacy_computer=false;release_generation=owner_generation;release_after_send=true;last_sent_ms=now_ms;
        finish_release();
        assert(!peer.sin_port && !ble_peer.sin_port && !ble_owner && !owner_client[0] && !owner_seen && !last_sent_ms);
        assert(hello_checked && !hello_authorized); /* Immediate explicit revocation notice to the old endpoints. */
        assert(owner_generation==old_generation+1 && !release_after_send && !v_status.ble_peer);
        receive(3,auth,ble,true);assert(!hello_authorized && post_count==before); /* Retained transport grants no application authority. */
        receive(2,ask,ble,true);assert(strstr(reply,"pending") && !v_status.connection_request.replacing);
        receive(2,pair,ble,true);assert(post_count==before); /* Still requires a fresh physical decision. */
        vibe_connect_decide(&v_status.connection_request,true,now_ms);
        receive(2,pair,ble,true);assert(post_count==before+1 && commit_ble);
        reset(true);receive(3,auth,ble,true);old_generation=owner_generation;
        release_generation=owner_generation-1;release_after_send=true;finish_release();
        assert(ble_owner && owner_generation==old_generation && owner_client[0]); /* Late release cannot revoke a replacement session. */
        release_generation=owner_generation;release_after_send=true;finish_release();
        assert(v_status.token[0] && !ble_owner); /* Ordinary disconnect retains the credential. */
    }
    const char *client_b="33333333-3333-3333-3333-333333333333";
    reset(true);receive(3,auth,a,false);
    assert(vibe_trusted_matches(&v_computers,client,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    assert(vibe_trusted_put(&v_computers,client_b,credential));v_legacy_computer=false;
    char auth_b[512];snprintf(auth_b,sizeof(auth_b),"{\"role\":\"pc\",\"type\":\"pc.status\",\"client\":\"%s\",\"device\":\"aabbccddeeff\",\"token\":\"%s\"}",client_b,credential);
    for (unsigned cycle=0;cycle<100;cycle++) {
        receive(3,auth,a,false);receive(3,auth,ble,true);
        uint32_t owner_a=owner_generation;unsigned posted_a=post_count;
        receive(3,auth_b,b,false);
        assert(hello_trusted && !hello_authorized && owner_generation==owner_a && post_count==posted_a);
        assert(!strcmp(owner_client,client) && ble_owner); /* Healthy A is not preempted. */
        receive(2,auth_b,b,false);assert(post_count==posted_a); /* Knowing a key grants no active commands. */
        now_ms+=15001; /* Both A paths have missed their lease, not just BLE. */
        receive(3,auth_b,b,false);
        assert(hello_authorized && hello_trusted && !strcmp(owner_client,client_b));
        assert(owner_generation!=owner_a && !ble_owner && !strcmp(v_status.token,credential));
        assert(v_computers.count==2); /* Handoff never revokes the other computer. */
        receive(3,auth,a,false);assert(hello_trusted && !hello_authorized);
        unsigned posted_b=post_count;receive(2,auth,a,false);assert(post_count==posted_b);
        now_ms+=15001;
        receive(3,auth,a,false);assert(hello_authorized && !strcmp(owner_client,client));
    }
    /* An idle device cannot be seized by an arbitrary delayed business command. */
    now_ms+=15001;unsigned before_idle=post_count;
    receive(2,auth_b,b,false);assert(post_count==before_idle && !strcmp(owner_client,client));
    v_model.recording=true;receive(3,auth_b,b,false);assert(!hello_authorized && hello_trusted);
    v_model.recording=false;
    receive(2,ask,b,false);receive(3,auth_b,b,false);
    assert(!hello_authorized && hello_trusted); /* Explicit confirmation has priority over auto-claim. */
    v_status.connection_request=(vibe_connection_request_t){0};
    receive(3,auth_b,b,false);assert(hello_authorized);
    assert(vibe_trusted_remove(&v_computers,client_b));v_status.token[0]=0;
    release_generation=owner_generation;release_after_send=true;finish_release();
    receive(3,auth_b,b,false);assert(!hello_authorized && !hello_trusted);
    receive(3,auth,a,false);assert(hello_authorized && hello_trusted && v_computers.count==1);
    const char *more[]={client_b,"44444444-4444-4444-4444-444444444444","55555555-5555-5555-5555-555555555555"};
    for (unsigned i=0;i<3;i++) {
        char key[65];memset(key,'b'+(int)i,64);key[64]=0;
        assert(vibe_trusted_put(&v_computers,more[i],key));
    }
    char full[512];snprintf(full,sizeof(full),"{\"type\":\"connection.request\",\"client\":\"66666666-6666-6666-6666-666666666666\",\"request\":\"%s\",\"device\":\"aabbccddeeff\",\"digest\":\"%s\",\"name\":\"New Mac\"}",request,credential);
    receive(2,full,b,false);assert(strstr(reply,"full") && v_status.connection_request.phase==V_CONNECT_NONE);
    assert(!strcmp(owner_client,client) && v_computers.count==4);
    receive(2,ask,a,false);assert(strstr(reply,"pending")); /* Existing computers can rotate their credential when full. */
    puts("Connection ingress: physical approval, credential commitment, timeout, refusal, busy, dual link, generation isolation and 100 unpair/re-pair cycles PASS");
}
