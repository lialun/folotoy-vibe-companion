#include <stddef.h>
#include <stdbool.h>
#include "hosts.h"
bool vibe_relay_receive(vibe_route_t route,const void *bytes,size_t size) { (void)route;(void)bytes;(void)size;return true; }
/* Real adapter, fake SDK: reproduce NimBLE's synchronous NOTIFY_TX callback.
 * The mutex stub rejects recursion instead of hanging the host test forever. */
#include "ble_sdk.h"
#include "../../main/vibe/ble.c"
#include <assert.h>
#include <stdlib.h>
vibe_status_t v_status;
QueueHandle_t v_queue=(void *)1;
struct ble_hs_cfg_stub ble_hs_cfg;
static uint64_t now=1000;
static bool held,tx_held,advertising,saved_identity;
static bool transition_held;
static unsigned mutexes;
static uint8_t saved_address[6];
static unsigned notifications,lost,terminated,mtu=247,deinits,fail_fragment,busy_attempts;
static vibe_ble_frame_t received;
static char name[24];
uint64_t vibe_now(void) { return now; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)(uintptr_t)(++mutexes); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)3; }
int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) { (void)timeout;if (s==(void *)1) { assert(!held);held=true; } else if (s==(void *)2) { assert(!tx_held);tx_held=true; }return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { if (s==(void *)1) { assert(held);held=false; } else if (s==(void *)2) { assert(tx_held);tx_held=false; }return 1; }
int xQueueSend(QueueHandle_t q,const void *p,unsigned timeout) { (void)q;(void)timeout;assert(((const vibe_msg_t *)p)->type==V_MSG_LINK_LOST);lost++;return 1; }
bool vibe_json_depth_ok(const char *p,size_t n) { (void)p;(void)n;return true; }
bool vibe_message_post_from(QueueHandle_t q,const char *p,size_t n,vibe_route_t r) { (void)q;(void)p;(void)n;(void)r;return true; }
int xTaskCreate(void (*entry)(void *),const char *n,unsigned size,void *arg,unsigned priority,TaskHandle_t *task) {
    (void)entry;(void)n;(void)size;(void)arg;(void)priority;(void)task;return 1;
}
void vTaskDelete(TaskHandle_t t) { (void)t; }
void vTaskDelay(unsigned ticks) { assert(!held);now+=ticks; }
int ble_gap_adv_active(void) { assert(!held);return advertising; }
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *p) { (void)p;return 0; }
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *p) { (void)p;return 0; }
int ble_gap_adv_start(uint8_t a,const void *b,int c,const struct ble_gap_adv_params *p,int (*cb)(struct ble_gap_event *,void *),void *arg) {
    (void)a;(void)b;(void)c;(void)p;(void)cb;(void)arg;advertising=true;return 0;
}
int ble_gap_adv_stop(void) { advertising=false;return 0; }
const char *ble_svc_gap_device_name(void) { return name; }
int os_mbuf_append(struct os_mbuf *m,const void *p,unsigned n) { memcpy(m->bytes,p,n);m->length=n;return 0; }
int ble_uuid_cmp(const ble_uuid_t *a,const ble_uuid_t *b) { return memcmp(a,b,sizeof(*a)); }
int ble_hs_mbuf_to_flat(struct os_mbuf *m,void *p,unsigned cap,uint16_t *n) { assert(cap>=m->length);memcpy(p,m->bytes,m->length);*n=m->length;return 0; }
int ble_gap_conn_find(uint16_t c,struct ble_gap_conn_desc *p) { (void)c;p->peer_id_addr=1;return 0; }
int ble_store_util_delete_peer(const void *p) { (void)p;return 0; }
int ble_hs_id_set_rnd(const uint8_t *p) { assert(!memcmp(p,saved_address,6));return 0; }
int ble_store_util_count(int kind,int *count) { (void)kind;*count=0;return 0; }
void nimble_port_run(void) {}
esp_err_t nimble_port_init(void) { assert(transition_held);return ESP_OK; }
esp_err_t nimble_port_deinit(void) { assert(transition_held);deinits++;return ESP_OK; }
esp_err_t esp_pm_lock_create(int kind,int arg,const char *name,esp_pm_lock_handle_t *out) { (void)arg;(void)name;assert(kind==ESP_PM_NO_LIGHT_SLEEP);*out=(void *)4;return ESP_OK; }
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t p) { assert(p==(void *)4 && !transition_held);transition_held=true;return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t p) { assert(p==(void *)4 && transition_held);transition_held=false;return ESP_OK; }
int nimble_port_stop(void) { return 0; }
int ble_store_util_status_rr(void *p) { (void)p;return 0; }
void ble_svc_gap_init(void) {}
void ble_svc_gatt_init(void) {}
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *p) { (void)p;return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *p) { (void)p;return 0; }
int ble_svc_gap_device_name_set(const char *p) { snprintf(name,sizeof(name),"%s",p);return 0; }
void ble_store_config_init(void) {}
int ble_gap_terminate(uint16_t h,int reason) { (void)h;(void)reason;assert(!held);terminated++;return 0; }
int ble_gap_update_params(uint16_t h,const struct ble_gap_upd_params *p) { (void)h;assert(!held);assert(p->latency==0 || p->latency==29);return 0; }
unsigned ble_att_mtu(uint16_t h) { (void)h;assert(!held);return mtu; }
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p,unsigned n) {
    struct os_mbuf *m=malloc(sizeof(*m));assert(m && n<=244);memcpy(m->bytes,p,n);m->length=n;return m;
}
int ble_gatts_notify_custom(uint16_t h,uint16_t attr,struct os_mbuf *m) {
    (void)h;(void)attr;notifications++;
    assert(!held && tx_held);struct ble_gap_event e={.type=BLE_GAP_EVENT_NOTIFY_TX};
    assert(gap(&e,NULL)==0); /* SDK callback occurs before notify returns. */
    if (busy_attempts) { busy_attempts--;free(m);return BLE_HS_ENOMEM; }
    if (notifications==fail_fragment) { free(m);return -1; }
    assert(vibe_ble_frame_add(&received,m->bytes,m->length,now)>=0);free(m);
    return 0;
}
esp_err_t nvs_open(const char *p,unsigned flags,nvs_handle_t *n) { (void)p;(void)flags;*n=1;return ESP_OK; }
void nvs_close(nvs_handle_t n) { (void)n; }
esp_err_t nvs_get_blob(nvs_handle_t n,const char *key,void *p,size_t *size) {
    (void)n;assert(!strcmp(key,"ble_identity"));if (!saved_identity) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size==6);memcpy(p,saved_address,6);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t n,const char *key,const void *p,size_t size) { (void)n;assert(!strcmp(key,"ble_identity") && size==6);memcpy(saved_address,p,6);saved_identity=true;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t n) { (void)n;return ESP_OK; }
void esp_fill_random(void *p,size_t n) { memset(p,0x52,n); }
static void connect_slot(unsigned h) {
    advertising=false;
    struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=h}};gap(&e,NULL);
    e=(struct ble_gap_event){.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=h,.attr_handle=tx_handle,.cur_notify=true}};gap(&e,NULL);
}
int main(void) {
    strcpy(v_status.device_id,"123456789abc");v_status.ble_enabled=true;
    assert(vibe_ble_init()==ESP_OK);
    assert(!started && deinits==0); /* Core/Wi-Fi allocations precede host pools. */
    assert(vibe_ble_enable(true));sync_host();assert(saved_identity && (identity[5]&0xc0)==0xc0);
    uint8_t original[6];memcpy(original,identity,6);tx_handle=7;
    struct ble_gap_event early={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=1,.attr_handle=tx_handle,.cur_notify=true}};
    gap(&early,NULL);vibe_route_t route=slots[0].route;
    struct ble_gap_event connected={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=1}};gap(&connected,NULL);
    assert(slots[0].subscribed && vibe_route_equal(slots[0].route,route) && !slots[1].used);
    /* Service Changed CCCD indications must not turn off our TX subscription. */
    early.subscribe.attr_handle=tx_handle+10;early.subscribe.cur_notify=false;gap(&early,NULL);
    assert(slots[0].subscribed);
    uint8_t packet[800];memset(packet,42,sizeof(packet));
    for (mtu=23;mtu<=247;mtu+=224) {
        notifications=0;vibe_ble_frame_clear(&received);
        assert(vibe_ble_send(route,1,packet,sizeof(packet))==0 && !held);
        assert(received.used==801 && received.bytes[0]==1 && !memcmp(received.bytes+1,packet,800));
        assert(notifications>1);
    }
    notifications=0;fail_fragment=2;assert(vibe_ble_send(route,1,packet,sizeof(packet))<0 && !held);fail_fragment=0;
    busy_attempts=3;assert(vibe_ble_send(route,1,packet,sizeof(packet))==0 && !held && !tx_held);
    busy_attempts=1000;uint64_t start=now;assert(vibe_ble_send(route,1,packet,sizeof(packet))<0 && now-start<=85);busy_attempts=0;
    connect_slot(2);now+=31000;vibe_ble_poll(now,false,route);assert(terminated==1);
    reset_host(42);assert(lost==2 && !slots[0].used && !slots[1].used && !held);
    connect_slot(1);assert(slots[0].route.serial!=route.serial);
    assert(vibe_ble_send(route,1,packet,sizeof(packet))<0); /* reused handle rejects old route */
    assert(!vibe_ble_enable(false));assert(!started && deinits==1 && lost==3 && !transition_held);
    assert(vibe_ble_enable(true));sync_host();assert(started && !memcmp(original,identity,6));
    vibe_ble_enable(false);vibe_ble_frame_clear(&received);
    puts("BLE runtime: restored CCCD before CONNECT, unrelated CCCD isolation, host API lock boundaries, synchronous notify callback, MTU fragmentation, bounded backpressure, partial send, host reset, handle reuse and stop/restart PASS");
}
