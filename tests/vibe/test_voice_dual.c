#include "voice_copy.h"
#include "peer.h"
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
static unsigned diagnostic_logs;
#define ESP_LOGI(tag,...) do {(void)(tag);++diagnostic_logs;if(0)printf(__VA_ARGS__);}while(0)
static pthread_mutex_t peer_lock=PTHREAD_MUTEX_INITIALIZER,copy_lock=PTHREAD_MUTEX_INITIALIZER;
static SemaphoreHandle_t peer_mutex=&peer_lock;
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait) {(void)wait;assert(!pthread_mutex_lock(s));return 1;}
static int xSemaphoreGive(SemaphoreHandle_t s) {assert(!pthread_mutex_unlock(s));return 1;}
static int udp=1;
static const char *TAG="test";
static struct sockaddr_in peer,ble_peer;
static bool ble_owner,release_after_send;
static uint32_t owner_generation=1;
static uint64_t wifi_seen=1000,last_sent_ms;
static atomic_uint_fast64_t now=1000;
static atomic_uint ble_generation=7,ble_calls=0,wifi_calls=0,wakes=0;
static bool udp_fails,ble_fails;
static uint8_t received[VIBE_VOICE_BODY+1];
static uint64_t vibe_now(void) {return atomic_load(&now);}
static uint32_t vibe_network_generation(void) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);uint32_t g=owner_generation;xSemaphoreGive(peer_mutex);return g;
}
static uint32_t vibe_ble_generation(uint16_t handle) {assert(handle==3);return atomic_load(&ble_generation);}
static bool vibe_ble_generation_matches(uint16_t handle,uint32_t g) {return g && g==vibe_ble_generation(handle);}
static void vibe_network_wake(void) {++wakes;}
static ssize_t test_sendto(int fd,const void *bytes,size_t n,int flags,const struct sockaddr *to,socklen_t len) {
    (void)to;assert(fd==1 && flags==MSG_DONTWAIT && len==sizeof(peer) && n==sizeof(received));
    memcpy(received,bytes,n);++wifi_calls;return udp_fails?-1:(ssize_t)n;
}
#define sendto test_sendto
static pthread_mutex_t barrier_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_cond=PTHREAD_COND_INITIALIZER;
static bool hold_ble,ble_entered;
static int vibe_ble_send_to(uint16_t handle,unsigned char type,const void *bytes,size_t n) {
    assert(handle==3 && type==6 && bytes && n==VIBE_VOICE_BODY);
    ++ble_calls;
    pthread_mutex_lock(&barrier_lock);
    ble_entered=true;pthread_cond_broadcast(&barrier_cond);
    while(hold_ble) pthread_cond_wait(&barrier_cond,&barrier_lock);
    pthread_mutex_unlock(&barrier_lock);
    return ble_fails?-1:0;
}
#include "../../main/vibe/voice_send.inc"
static void *flush_thread(void *unused) {(void)unused;voice_copy_flush();return NULL;}
int main(void) {
    uint8_t block[ADPCM_BLOCK_BYTES]={0},body[VIBE_VOICE_BODY];
    const uint64_t session=UINT64_C(0x123456789abcdef0);
    assert(vibe_voice_encode(session,256,block,sizeof(block),body,sizeof(body))==sizeof(body));
    assert(body[0]==0xf0 && body[7]==0x12 && body[8]==0 && body[9]==1 && body[10]==0);
    assert(!vibe_voice_encode(0,0,block,sizeof(block),body,sizeof(body)));
    assert(!vibe_voice_encode(session,2000,block,sizeof(block),body,sizeof(body)));
    assert(!vibe_voice_encode(session,1,block,sizeof(block)-1,body,sizeof(body)));
    block[2]=89;assert(!vibe_voice_encode(session,1,block,sizeof(block),body,sizeof(body)));block[2]=0;
    voice_copy_mutex=&copy_lock;peer.sin_addr.s_addr=inet_addr("192.0.2.1");peer.sin_port=htons(33334);
    ble_peer.sin_port=htons(4);ble_owner=true;
    assert(!vibe_send_voice(body,sizeof(body),1) && wifi_calls==1 && ble_calls==0 && voice_pending.valid);
    assert(received[0]==6 && !memcmp(received+1,body,sizeof(body)));
    voice_copy_flush();assert(ble_calls==1 && !voice_pending.valid);
    assert(!vibe_send_voice(body,sizeof(body),1));assert(!vibe_send_voice(body,sizeof(body),1));
    assert(voice_tx.replaced==1);now=1201;voice_copy_flush();assert(ble_calls==1 && voice_tx.expired==1);
    now=1250;assert(!vibe_send_voice(body,sizeof(body),1));++owner_generation;voice_copy_flush();assert(ble_calls==1);
    assert(vibe_send_voice(body,sizeof(body),1)==-1);owner_generation=1;
    assert(!vibe_send_voice(body,sizeof(body),1));++ble_generation;voice_copy_flush();assert(ble_calls==1);
    assert(!vibe_send_voice(body,sizeof(body),1));release_after_send=true;voice_copy_flush();assert(ble_calls==1);release_after_send=false;
    /* Failed UDP submission immediately uses BLE without waiting for a worker. */
    udp_fails=true;assert(!vibe_send_voice(body,sizeof(body),1) && ble_calls==2);udp_fails=false;
    /* BLE alone retains the original direct path. */
    now=8000;unsigned before=wifi_calls;assert(!vibe_send_voice(body,sizeof(body),1) && ble_calls==3 && wifi_calls==before);
    ble_fails=true;assert(!vibe_send_voice(body,sizeof(body),1) && voice_pending.valid);
    assert(voice_copy_wait(250)==20 && voice_copy_wait(10)==10);
    uint64_t born=voice_pending.born;now+=40;voice_copy_flush();
    assert(voice_pending.valid && voice_pending.born==born && voice_tx.retry_queued==2);
    now+=40;ble_fails=false;voice_copy_flush();assert(!voice_pending.valid);
    assert(voice_copy_wait(250)==250);
    ble_fails=true;assert(!vibe_send_voice(body,sizeof(body),1));
    now+=201;voice_copy_flush();assert(!voice_pending.valid);ble_fails=false;
    /* An older failed in-flight frame cannot displace a newer pending frame. */
    vibe_voice_copy_t old={0},newer={0};
    vibe_voice_copy_offer(&old,body,sizeof(body),now,1,3,ble_generation);
    vibe_voice_copy_offer(&newer,body,sizeof(body),now+1,1,3,ble_generation);
    assert(!vibe_voice_copy_retry(&newer,&old,now+2,1) && newer.born==now+1);
    newer.valid=false;assert(!vibe_voice_copy_retry(&newer,&old,now+201,1));
    assert(!vibe_voice_copy_retry(&newer,&old,now,2));
    release_after_send=true;assert(vibe_send_voice(body,sizeof(body),1)==-1);release_after_send=false;
    /* An arbitrarily blocked backup cannot own the fast path's locks. */
    wifi_seen=now;assert(!vibe_send_voice(body,sizeof(body),1));
    pthread_mutex_lock(&barrier_lock);hold_ble=true;ble_entered=false;pthread_mutex_unlock(&barrier_lock);
    pthread_t thread;assert(!pthread_create(&thread,NULL,flush_thread,NULL));
    pthread_mutex_lock(&barrier_lock);while(!ble_entered)pthread_cond_wait(&barrier_cond,&barrier_lock);pthread_mutex_unlock(&barrier_lock);
    before=wifi_calls;
    for(unsigned i=0;i<1000;i++) assert(!vibe_send_voice(body,sizeof(body),1));
    assert(wifi_calls==before+1000 && voice_pending.valid);
    pthread_mutex_lock(&barrier_lock);hold_ble=false;pthread_cond_broadcast(&barrier_cond);pthread_mutex_unlock(&barrier_lock);
    assert(!pthread_join(thread,NULL));voice_copy_flush();assert(!voice_pending.valid);
    ble_owner=false;peer.sin_addr.s_addr=0;assert(vibe_send_voice(body,sizeof(body),1)==-1);
    assert(!diagnostic_logs);vibe_voice_report();assert(diagnostic_logs==1);
    puts("Voice V2 wire, bounded backup, owner/link generation, stale copy, one-link paths and blocked-BLE independence PASS");
}
