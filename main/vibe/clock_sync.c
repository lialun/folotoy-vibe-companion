#include "clock_sync.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include <time.h>

void vibe_clock_poll(vibe_clock_sync_t *s,uint64_t now,bool wifi,bool paused) {
    static const char *const servers[]={"ntp.aliyun.com","time.cloudflare.com","pool.ntp.org"};
    const char *tag="vibe_clock";
    bool valid=vibe_clock_valid(time(NULL)),allowed=!paused;
    /* Leave the radio's 16 KiB control/network reserve plus 4 KiB headroom.
     * A busy heap defers a new request without rotating servers or retry spam. */
    if (!s->active && wifi && !valid && now>=s->deadline && allowed)
        allowed=esp_get_free_heap_size()>=20*1024;
    switch (vibe_clock_step(s,now,wifi,valid,allowed)) {
    case V_CLOCK_START: {
        esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG(servers[s->server]);
        config.wait_for_sync=false; /* No semaphore, callback or extra task. */
        esp_err_t result=esp_netif_sntp_init(&config);
        unsigned reachability=0;
        if (result==ESP_OK && esp_netif_sntp_reachability(0,&reachability)!=ESP_OK) {
            esp_netif_sntp_deinit();result=ESP_ERR_NO_MEM;
        }
        if (result!=ESP_OK) {
            vibe_clock_failed(s,now);
            ESP_LOGW(tag,"Network clock deferred: error=%d",result);
        } else ESP_LOGI(tag,"Network clock started: server=%u free=%u",s->server+1,(unsigned)esp_get_free_heap_size());
        break;
    }
    case V_CLOCK_STOP:
        /* The netif API joins TCP/IP cleanup before releasing its storage. */
        esp_netif_sntp_deinit();
        ESP_LOGI(tag,"Network clock stopped: valid=%u free=%u",valid,(unsigned)esp_get_free_heap_size());
        break;
    case V_CLOCK_NONE: break;
    }
}
