#include "vibe.h"
#include "voice_uplink.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t ring_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t barrier_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier=PTHREAD_COND_INITIALIZER;
static atomic_uint_fast64_t clock_ms=1000;
static uint32_t owner=7;
static unsigned live_links=3, sends[2], allocations, wakes;
static bool hold_ble, entered_ble, fail_ble, fail_wifi, fail_memory,partial_ble;
static bool power_active;
static unsigned power_starts,power_stops;
static bool capture_during_wifi;
static unsigned capture_delay_ms;
static uint64_t capture_session;
static uint32_t ble_sequence;
static void enqueue(uint64_t session,uint32_t sequence);

static void *allocate(size_t size) {
    if (fail_memory) return NULL;
    void *value=malloc(size);
    if (value) ++allocations;
    return value;
}
static void release(void *value) {assert(value && allocations);--allocations;free(value);}
static vibe_voice_tx_slot_t *storage_acquire(unsigned *count) {*count=V_VOICE_TX_SLOTS;return allocate(*count*sizeof(vibe_voice_tx_slot_t));}
static void storage_release(vibe_voice_tx_slot_t *slots) {release(slots);}
#define malloc allocate
#define free release
#include "../../main/vibe/voice_uplink.c"
#undef malloc
#undef free

SemaphoreHandle_t xSemaphoreCreateMutex(void) {return &ring_lock;}
int xSemaphoreTake(SemaphoreHandle_t value,unsigned wait) {
    (void)wait;assert(value==&ring_lock);assert(!pthread_mutex_lock(value));return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t value) {assert(!pthread_mutex_unlock(value));return pdTRUE;}
size_t heap_caps_get_free_size(unsigned caps) {(void)caps;return 65536;}
size_t heap_caps_get_largest_free_block(unsigned caps) {(void)caps;return 65536;}
uint64_t vibe_now(void) {return atomic_load(&clock_ms);}
vibe_ble_voice_pressure_t vibe_ble_voice_pressure(void) {return (vibe_ble_voice_pressure_t){0};}
vibe_ble_radio_probe_t vibe_ble_radio_probe(void) {return (vibe_ble_radio_probe_t){0};}
void vibe_ble_probe_recording(bool recording) {(void)recording;}
void vibe_ble_voice_report(void) {}
void vibe_ble_voice_link(vibe_voice_counters_t *value) {(void)value;}
static uint32_t current_owner(void) {return owner;}
static unsigned links(uint32_t expected,vibe_voice_mode_t mode) {return expected==owner?(mode==V_VOICE_LEGACY ? (live_links&1?1:live_links&2):live_links):0;}
static void wake(void) {++wakes;}
static void platform_activity(bool active) {
    assert(!pthread_mutex_trylock(&ring_lock));pthread_mutex_unlock(&ring_lock);
    assert(active!=power_active);power_active=active;
    if(active)++power_starts;else ++power_stops;
}
static vibe_voice_submission_t send_frame(vibe_voice_mode_t mode,const vibe_voice_tx_ticket_t *ticket,const uint8_t *bytes,size_t length,int *error) {
    unsigned leg=ticket->leg,type=mode==V_VOICE_LEGACY?1:6;uint32_t expected=ticket->owner;
    assert(expected==owner && bytes && length==VIBE_VOICE_BODY && leg<2);
    assert(!pthread_mutex_trylock(&ring_lock));pthread_mutex_unlock(&ring_lock);
    uint8_t packet[VIBE_VOICE_BODY+1];
    size_t count=vibe_voice_datagram(type,bytes,length,packet,sizeof(packet));
    assert(count==(type==1 ? 807:817) && packet[0]==type);
    ++sends[leg];
    if (leg==1) {
        ble_sequence=ticket->sequence;
        pthread_mutex_lock(&barrier_lock);
        entered_ble=true;pthread_cond_broadcast(&barrier);
        while (hold_ble) pthread_cond_wait(&barrier,&barrier_lock);
        pthread_mutex_unlock(&barrier_lock);
        return partial_ble?V_VOICE_PARTIAL:fail_ble?V_VOICE_NOT_SUBMITTED:V_VOICE_SUBMITTED;
    }
    if (capture_during_wifi) {
        capture_during_wifi=false;
        clock_ms+=capture_delay_ms;
        enqueue(capture_session,1);
    }
    if (fail_wifi) {*error=ENOMEM;return V_VOICE_NOT_SUBMITTED;}
    return V_VOICE_SUBMITTED;
}
static void enqueue(uint64_t session,uint32_t sequence) {
    uint8_t block[ADPCM_BLOCK_BYTES]={0},frame[VIBE_VOICE_BODY];
    assert(vibe_voice_encode(session,sequence,block,sizeof(block),frame,sizeof(frame))==sizeof(frame));
    assert(!vibe_send_voice(frame,sizeof(frame),owner));
}
static void *service_thread(void *unused) {(void)unused;vibe_voice_uplink_service(true);return NULL;}

int main(void) {
    assert(vibe_voice_uplink_init((vibe_voice_uplink_ports_t){0})==ESP_ERR_INVALID_ARG);
    assert(vibe_voice_uplink_init((vibe_voice_uplink_ports_t){.owner=current_owner,.links=links,.send=send_frame,.wake=wake,.activity=platform_activity,.acquire=storage_acquire,.release=storage_release})==ESP_OK);
    assert(!vibe_voice_begin(1,owner,(vibe_voice_mode_t)99,false) && allocations==0);
    assert(vibe_voice_begin(1,owner,V_VOICE_STREAM,false)==V_VOICE_TX_SLOTS && allocations==1);
    assert(!vibe_voice_uplink_is_ready(1,owner) && !vibe_voice_uplink_can_overtake_control());
    vibe_voice_uplink_ready(2,owner);assert(!vibe_voice_uplink_is_ready(1,owner));
    vibe_voice_uplink_ready(1,owner);assert(vibe_voice_uplink_is_ready(1,owner) && vibe_voice_uplink_can_overtake_control());
    enqueue(1,0);
    assert(sends[0]==0 && sends[1]==0); /* Capture has no transport side effects. */
    hold_ble=true;
    pthread_t thread;assert(!pthread_create(&thread,NULL,service_thread,NULL));
    pthread_mutex_lock(&barrier_lock);
    while (!entered_ble) pthread_cond_wait(&barrier,&barrier_lock);
    pthread_mutex_unlock(&barrier_lock);
    assert(sends[0]==1 && power_active && power_starts==1);
    for (unsigned seq=1;seq<10;seq++) enqueue(1,seq);
    /* Capture and a new request remain operable during an in-flight old send. */
    vibe_voice_end(1,owner);
    assert(vibe_voice_begin(2,owner,V_VOICE_STREAM,false)==V_VOICE_TX_SLOTS && allocations==1);
    enqueue(2,0);
    pthread_mutex_lock(&barrier_lock);hold_ble=false;pthread_cond_broadcast(&barrier);pthread_mutex_unlock(&barrier_lock);
    assert(!pthread_join(thread,NULL));
    assert(tx.session==2 && tx.sent[1]==0 && tx.slots[0].pending==3);
    assert(power_active && power_starts==1 && !power_stops); /* Replacement reuses the lease. */
    fail_wifi=true;fail_ble=false;clock_ms+=20;
    vibe_voice_uplink_service(true);
    assert(tx.sent[1]==1 && tx.sent[0]==0 && tx.slots[0].pending==1);
    unsigned sent=sends[0];
    vibe_voice_uplink_service(true);assert(sends[0]==sent);
    fail_wifi=false;clock_ms+=20;vibe_voice_uplink_service(true);
    assert(tx.sent[0]==1 && !vibe_voice_tx_pending(&tx));
    vibe_voice_uplink_cancel(1,owner);
    assert(!vibe_voice_uplink_cancelled(2,owner));
    live_links=V_VOICE_BLE;partial_ble=true;enqueue(2,1);
    clock_ms+=20;vibe_voice_uplink_service(true);
    assert(tx.leased==V_VOICE_BLE);
    vibe_voice_uplink_cancel(2,owner);
    assert(vibe_voice_uplink_cancelled(2,owner) && !tx.leased && !vibe_voice_tx_pending(&tx) && !vibe_voice_uplink_can_overtake_control());
    clock_ms+=20;vibe_voice_uplink_service(false);
    assert(!power_active && power_stops==1); /* Cleanup is independent of media permission. */
    partial_ble=false;live_links=3;
    vibe_voice_counters_t counters=vibe_voice_counters();
    assert(counters.wifi_oom==1 && counters.wifi_error==ENOMEM && counters.ble>=1);
    vibe_voice_end(2,owner);clock_ms+=601;vibe_voice_uplink_service(true);
    assert(!allocations && !tx.slots && vibe_voice_uplink_wait(30000)==30000);
    fail_memory=true;assert(!vibe_voice_begin(3,owner,V_VOICE_STREAM,false) && !allocations);
    fail_memory=false;assert(vibe_voice_begin(3,owner,V_VOICE_LEGACY,false)==V_VOICE_TX_SLOTS);
    enqueue(3,0);
    unsigned ble_before=sends[1];clock_ms+=20;vibe_voice_uplink_service(true);
    assert(wire_mode==V_VOICE_LEGACY && sends[1]==ble_before && tx.sent[0]==1);
    owner=8;clock_ms+=20;vibe_voice_uplink_service(true);
    assert(!allocations && wakes>0 && !power_active && power_starts==power_stops);
    for(unsigned cycle=0;cycle<100;cycle++) {
        uint64_t session=100+cycle;
        assert(vibe_voice_begin(session,owner,V_VOICE_STREAM,false)==V_VOICE_TX_SLOTS);
        unsigned sent_before=sends[0]+sends[1];
        clock_ms+=20;vibe_voice_uplink_service(false);
        assert(power_active && sends[0]+sends[1]==sent_before); /* Preparing is already performance-sensitive. */
        vibe_voice_end(session,owner);
        clock_ms+=V_VOICE_RETAIN_MS-1;vibe_voice_uplink_service(false);
        assert(power_active && tx.slots); /* Tail repair keeps the same lease. */
        clock_ms+=20;vibe_voice_uplink_service(false);
        assert(!power_active && !tx.slots && !allocations && power_starts==power_stops);
    }
    /* A stack call may yield while capture publishes a newer block. The BLE
     * decision must use its own current time, including expiry boundaries. */
    const unsigned delays[]={0,1,20,100,120,249,250,251,599,600,601,700};
    for(unsigned i=0;i<sizeof(delays)/sizeof(delays[0]);i++) for(unsigned failed=0;failed<2;failed++) {
        capture_session=500+2*i+failed;capture_delay_ms=delays[i];
        assert(vibe_voice_begin(capture_session,owner,V_VOICE_STREAM,false)==V_VOICE_TX_SLOTS);
        enqueue(capture_session,0);capture_during_wifi=true;fail_wifi=failed;
        ble_sequence=UINT32_MAX;clock_ms+=20;vibe_voice_uplink_service(true);
        assert(!capture_during_wifi && ble_sequence!=UINT32_MAX);
        /* The new block is either already delivered or still eligible; a
         * timestamp from before the Wi-Fi call cannot expire its BLE copy. */
        assert(ble_sequence==1 || (tx.slots[1].pending&V_VOICE_BLE));
        clock_ms+=20;vibe_voice_uplink_service(true);
        assert(ble_sequence==1 && !(tx.slots[1].pending&V_VOICE_BLE));
        vibe_voice_end(capture_session,owner);clock_ms+=V_VOICE_RETAIN_MS+1;
        vibe_voice_uplink_service(false);
        assert(!allocations && !power_active && power_starts==power_stops);
    }
    puts("Production uplink: capture isolation, per-carrier clock freshness, lock ownership, replacement, backoff and bounded performance lifetime PASS");
}
