/* Production lifecycle with SDK ports replaced by ordered fault-injection stubs. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_err.h"
#include "ble_links.h"
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
#define portMAX_DELAY 1000
#define pdMS_TO_TICKS(n) (n)
#define pdTRUE 1
#define pdPASS 1
#define ESP_LOG_WARN 2
#define NIMBLE_HS_STACK_SIZE 4096
#define NIMBLE_CORE 0
#define configMAX_PRIORITIES 25
#define BLE_HS_IO_NO_INPUT_OUTPUT 3
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define LINK_SLOTS VIBE_BLE_LINK_SLOTS
static SemaphoreHandle_t tx_mutex,links_mutex,metadata_mutex;
static vibe_ble_links_t registry;
#define links registry.links
static atomic_bool ready,ble_enabled,npl_initialized;
static int parameter_callout,streaming_event,radio_clear_event;
static struct {bool active;} voice_cursor;
static uint16_t voice_probe_handle=UINT16_MAX;
static struct {unsigned bytes,header,leading;} att_layout;
static struct {char device_id[18];} v_status={"4c11ae2f6bac"};
static int services[1];
static struct {
    void (*sync_cb)(void);void (*reset_cb)(int);void (*store_status_cb)(void);
    int sm_io_cap,sm_mitm,sm_sc,sm_bonding,sm_our_key_dist,sm_their_key_dist;
} ble_hs_cfg;
static unsigned fault,mutexes,locks,objects,ports,hosts,starts,stops,disposals,notifications;
static bool force_join_timeout;
static void (*host_entry)(void *);
static void esp_log_level_set(const char *tag,int level) {(void)tag;assert(level==2);}
static SemaphoreHandle_t xSemaphoreCreateMutex(void) {++mutexes;return fault==mutexes?NULL:(void *)(uintptr_t)mutexes;}
static int xSemaphoreTake(SemaphoreHandle_t m,int timeout) {(void)timeout;assert(m);++locks;return 1;}
static int xSemaphoreGive(SemaphoreHandle_t m) {assert(m && locks);--locks;return 1;}
static void *nimble_port_get_dflt_eventq(void) {assert(ports);return (void *)99;}
static void ble_npl_callout_init(int *p,void *q,void (*cb)(void *),void *arg) {(void)cb;(void)arg;assert(q && !*p);*p=1;++objects;}
static void ble_npl_event_init(int *p,void (*cb)(void *),void *arg) {(void)cb;(void)arg;assert(!*p);*p=1;++objects;}
static void ble_npl_callout_deinit(int *p) {assert(!hosts && *p && objects);*p=0;--objects;}
static void ble_npl_event_deinit(int *p) {assert(!hosts && *p && objects);*p=0;--objects;}
static void parameter_update_event(void *p) {(void)p;}
static void streaming_update_event(void *p) {(void)p;}
static void radio_clear(void *p) {(void)p;}
static void on_sync(void) {if(ble_enabled)ready=true;}
static void on_reset(int n) {(void)n;ready=false;}
static void ble_store_util_status_rr(void) {}
static void ble_svc_gap_init(void) {assert(ports);}
static void ble_svc_gatt_init(void) {assert(ports);}
static void ble_store_config_init(void) {assert(ports);}
static void ble_svc_gap_device_name_set(const char *name) {assert(!strcmp(name,"Vibe-4c11ae2f6bac"));}
static int ble_gatts_count_cfg(void *p) {assert(p==services);return fault==5?-1:0;}
static int ble_gatts_add_svcs(void *p) {assert(p==services);return fault==6?-1:0;}
static void ble_att_set_preferred_mtu(unsigned mtu) {assert(mtu==247);}
static int vibe_discovery_json(char *out,size_t cap,bool a,bool b,bool c) {assert(!a&&!b&&!c);return snprintf(out,cap,"{}");}
static void vibe_ble_publish_metadata(const char *text,size_t n) {assert(metadata_mutex && n==2 && !strcmp(text,"{}"));}
static esp_err_t nimble_port_init(void) {assert(!ports && !hosts);++starts;if(fault==4)return ESP_ERR_NO_MEM;++ports;return ESP_OK;}
static esp_err_t nimble_port_deinit(void) {assert(ports && !hosts && !objects && !locks);++disposals;if(fault==10)return ESP_FAIL;--ports;return ESP_OK;}
static int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned bytes,void *arg,int priority,void *handle,int core) {
    assert(!hosts && ports && ble_enabled && !strcmp(name,"nimble_host") && bytes==4096 && !arg && priority==21 && !handle && core==0);
    if(fault==7)return 0;
    ++hosts;host_entry=entry;ble_hs_cfg.sync_cb();return 1;
}
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return (void *)42;}
static void xTaskNotifyGive(TaskHandle_t task) {assert(task==(void *)42);++notifications;}
static unsigned ulTaskNotifyTake(int clear,unsigned timeout) {
    assert(clear);if(timeout && force_join_timeout)return 0;
    unsigned result=notifications;notifications=0;return result;
}
static void nimble_port_run(void) {assert(!ble_enabled && !ready && hosts);}
static void vTaskDelete(void *task) {assert(!task && hosts);--hosts;}
static void vTaskDelay(unsigned ticks) {assert(ticks==1 && !hosts && !ports);}
static int nimble_port_stop(void) {
    assert(!ble_enabled && !ready && hosts && !locks);++stops;
    if(fault==8)return -1;
    host_entry(NULL);return 0;
}
#include "../../main/vibe/ble_lifecycle.inc"
static void reset(void) {
    tx_mutex=links_mutex=metadata_mutex=NULL;ready=ble_enabled=npl_initialized=false;
    ble_prepared=stack_initialized=host_running=stack_stopping=stack_faulted=false;
    atomic_store(&stop_waiter,NULL);parameter_callout=streaming_event=radio_clear_event=0;
    fault=mutexes=locks=objects=ports=hosts=starts=stops=disposals=notifications=0;force_join_timeout=false;
    vibe_ble_links_init(&registry);
}
int main(void) {
    for(unsigned at=1;at<=7;at++) {
        reset();fault=at;assert(vibe_ble_init()!=ESP_OK && !hosts && !ports && !objects && !ble_enabled);
        fault=0;assert(vibe_ble_init()==ESP_OK);assert(vibe_ble_pause()==ESP_OK);
    }
    reset();assert(vibe_ble_init()==ESP_OK);unsigned created=mutexes;
    for(unsigned cycle=0;cycle<100;cycle++) {
        assert(vibe_ble_init()==ESP_OK && hosts==1 && ports==1 && objects==3);
        vibe_ble_link_t *link=vibe_ble_link_ensure(&registry,1);uint32_t generation=link->generation;
        voice_cursor.active=true;voice_probe_handle=1;att_layout.bytes=64;
        assert(vibe_ble_pause()==ESP_OK && !hosts && !ports && !objects && !ble_enabled && !ready);
        assert(!voice_cursor.active && voice_probe_handle==UINT16_MAX && !att_layout.bytes && !vibe_ble_link_find(&registry,1));
        unsigned stopped=stops;assert(vibe_ble_pause()==ESP_OK && stops==stopped);
        assert(vibe_ble_init()==ESP_OK && mutexes==created);
        assert(vibe_ble_link_ensure(&registry,1)->generation>generation);
    }
    fault=8;assert(vibe_ble_pause()==ESP_FAIL && hosts && ports && ready && ble_enabled);
    fault=0;force_join_timeout=true;assert(vibe_ble_pause()==ESP_ERR_TIMEOUT && stack_stopping && !hosts && ports);
    unsigned stopped=stops;force_join_timeout=false;assert(vibe_ble_init()==ESP_OK && stops==stopped && ports && hosts);
    assert(vibe_ble_pause()==ESP_OK);
    assert(vibe_ble_init()==ESP_OK);fault=10;assert(vibe_ble_pause()==ESP_FAIL && stack_faulted && !hosts && !objects);
    unsigned disposed=disposals;assert(vibe_ble_init()==ESP_ERR_INVALID_STATE && vibe_ble_pause()==ESP_ERR_INVALID_STATE && disposals==disposed);
    puts("BLE lifecycle: init faults, TX admission, host join, NPL teardown, stable resources, generations and 100 pause/resume cycles PASS");
}
