#include <assert.h>
#include <stdio.h>
#include <string.h>
#define time test_time
#include "../../main/vibe/clock_sync.c"
#undef time

static time_t epoch;
static uint32_t heap=40000;
static unsigned starts,stops;
static bool allocated,fail_init,fail_pcb;
static const char *last_server;
time_t test_time(time_t *out) { if (out) *out=epoch;return epoch; }
uint32_t esp_get_free_heap_size(void) { return heap; }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) {
    assert(!allocated && c->start && !c->wait_for_sync && !c->sync_cb && !c->renew_servers_after_new_IP);
    ++starts;last_server=c->servers[0];
    if (fail_init) return ESP_ERR_NO_MEM;
    allocated=true;return ESP_OK;
}
esp_err_t esp_netif_sntp_reachability(unsigned index,unsigned *reachability) {
    assert(allocated && !index);*reachability=0;return fail_pcb?ESP_ERR_NO_MEM:ESP_OK;
}
void esp_netif_sntp_deinit(void) { assert(allocated);allocated=false;++stops; }
int main(void) {
    vibe_clock_sync_t s={0};
    assert(!vibe_clock_valid(1699999999) && vibe_clock_valid(1700000000));
    assert(vibe_clock_valid(4102444800LL) && !vibe_clock_valid(4102444801LL));
    vibe_clock_poll(&s,100,false,false);assert(!starts && vibe_clock_wait(&s,100,30000)==30000);
    epoch=1800000000;vibe_clock_poll(&s,100,true,false);assert(!starts && !s.deadline);
    epoch=0;vibe_clock_poll(&s,100,true,false);
    assert(starts==1 && allocated && s.active && !strcmp(last_server,"ntp.aliyun.com"));
    assert(vibe_clock_wait(&s,100,30000)==1000 && vibe_clock_wait(&s,12099,30000)==1);
    /* A valid clock may come from SNTP or the authenticated computer. Both stop requests. */
    epoch=1800000000;vibe_clock_poll(&s,500,true,false);
    assert(stops==1 && !allocated && !s.active && !s.deadline && !s.failures);
    for (uint64_t t=1000;t<86400000;t+=30000) vibe_clock_poll(&s,t,true,false);
    assert(starts==1 && vibe_clock_wait(&s,86400000,30000)==30000);
    epoch=0;vibe_clock_poll(&s,1000,true,false);assert(allocated);
    vibe_clock_poll(&s,12999,true,false);assert(allocated);
    vibe_clock_poll(&s,13000,true,false);assert(!allocated && s.failures==1 && s.deadline==18000);
    vibe_clock_poll(&s,17999,true,false);assert(!allocated);
    vibe_clock_poll(&s,18000,true,false);assert(allocated && !strcmp(last_server,"time.cloudflare.com"));
    vibe_clock_poll(&s,30000,true,false);assert(!allocated && s.failures==2 && s.deadline==90000);
    vibe_clock_poll(&s,90000,true,false);assert(allocated && !strcmp(last_server,"pool.ntp.org"));
    vibe_clock_poll(&s,102000,true,false);assert(!allocated && s.failures==3 && s.deadline==402000);
    for (unsigned i=0;i<20;i++) {
        uint64_t due=s.deadline;vibe_clock_poll(&s,due,true,false);assert(allocated);
        vibe_clock_poll(&s,due+12000,true,false);assert(!allocated && s.failures==4 && s.deadline==due+912000);
    }
    /* Offline after an expired deadline must keep the ordinary receive wait. */
    vibe_clock_poll(&s,s.deadline+100,false,false);
    assert(!s.deadline && vibe_clock_wait(&s,999999999,30000)==30000);
    vibe_clock_poll(&s,999999999,true,false);assert(allocated);
    vibe_clock_poll(&s,1000000000,false,false);assert(!allocated && !s.active && !s.deadline);
    /* Capture/setup cancels transient SNTP resources, then resumes without failure escalation. */
    vibe_clock_poll(&s,1000000001,true,false);assert(allocated);
    unsigned failures=s.failures;vibe_clock_poll(&s,1000000002,true,true);
    assert(!allocated && !s.active && s.failures==failures);
    uint64_t t=s.deadline;vibe_clock_poll(&s,t,true,true);assert(!allocated && s.deadline==t+30000);
    t=s.deadline;heap=20479;vibe_clock_poll(&s,t,true,false);assert(!allocated && s.failures==failures);
    t=s.deadline;heap=20480;vibe_clock_poll(&s,t,true,false);assert(allocated);
    epoch=1800000000;vibe_clock_poll(&s,t+1,true,false);assert(!allocated && !s.failures);
    /* Failed initialization and a missing UDP PCB both back off and leave no owner. */
    epoch=0;fail_init=true;t+=2;vibe_clock_poll(&s,t,true,false);assert(!allocated && !s.active && s.failures==1);
    fail_init=false;fail_pcb=true;t=s.deadline;vibe_clock_poll(&s,t,true,false);
    assert(!allocated && !s.active && s.failures==2);
    fail_pcb=false;
    for (unsigned i=0;i<100;i++) {
        epoch=0;t=s.deadline+1;vibe_clock_poll(&s,t,true,false);assert(allocated);
        epoch=1800000000;vibe_clock_poll(&s,t+1,true,false);assert(!allocated && !s.active && !s.deadline);
    }
    assert(starts==stops+1); /* One deliberately failed init had no resources. */
    puts("Network clock: completion/PC time, timeout/backoff, server rotation, disconnect, capture, low heap, failure cleanup and 100 cycles PASS");
}
