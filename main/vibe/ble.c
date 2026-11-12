#include "vibe.h"
#include "ble.h"
#include "ble_frame.h"
#include "radio/relay.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_pm.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

/* 6F6C6F56-6962-436F-8D11-0B4DE2C07400; suffix 01 info / 02 RX / 03 TX. */
#define UUID(s) BLE_UUID128_INIT(s,0x74,0xc0,0xe2,0x4d,0x0b,0x11,0x8d,0x6f,0x43,0x62,0x69,0x56,0x6f,0x6c,0x6f)
static const ble_uuid128_t service_uuid=UUID(0),info_uuid=UUID(1),rx_uuid=UUID(2),tx_uuid=UUID(3);
typedef struct { bool used,subscribed,fast,parameters_set;vibe_route_t route;vibe_ble_frame_t rx;uint64_t opened; } slot_t;
static slot_t slots[2];
/* State lock is never held across NimBLE APIs: GATT access can run while
 * the host owns its own lock. tx_lock serializes packet fragments and teardown;
 * callbacks never take tx_lock, so synchronous host callbacks cannot invert it. */
static SemaphoreHandle_t lock,tx_lock,host_done;
static esp_pm_lock_handle_t transition_lock;
static uint16_t tx_handle,message_id;
static uint32_t serial;
static uint8_t address_type;
static uint8_t identity[6];
static bool wanted=true,synced,started;
static uint64_t retry_after;
static const char *TAG="vibe_ble";
void ble_store_config_init(void);
static int gap(struct ble_gap_event *event,void *arg);
static slot_t *find(uint16_t handle) {
    for (unsigned i=0;i<2;i++) if (slots[i].used && slots[i].route.handle==handle) return &slots[i];
    return NULL;
}
/* IDF can restore encrypted CCCDs before its delayed CONNECT event. Preserve
 * the slot created by that early subscription instead of resetting it later. */
static slot_t *ensure_slot(uint16_t handle) {
    slot_t *s=find(handle);if (s) return s;
    for (unsigned i=0;i<2;i++) if (!slots[i].used) {
        slots[i]=(slot_t){.used=true,.route={.kind=V_LINK_BLE,.handle=handle,.serial=++serial},.opened=vibe_now()};return &slots[i];
    }
    return NULL;
}
static void forget_slot(slot_t *s) {
    if (s->used) {
        vibe_msg_t m={.type=V_MSG_LINK_LOST,.source=s->route};(void)xQueueSend(v_queue,&m,0);
    }
    vibe_ble_frame_clear(&s->rx);memset(s,0,sizeof(*s));
}
static void advertise(void) {
    xSemaphoreTake(lock,portMAX_DELAY);
    unsigned count=0;for (unsigned i=0;i<2;i++) count+=slots[i].used;
    bool allowed=wanted && synced && count<2;
    xSemaphoreGive(lock);
    if (!allowed || ble_gap_adv_active()) return;
    struct ble_hs_adv_fields f={0};f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t *)&service_uuid;f.num_uuids128=1;f.uuids128_is_complete=1;
    if (ble_gap_adv_set_fields(&f)) return;
    struct ble_hs_adv_fields response={0};const char *name=ble_svc_gap_device_name();
    response.name=(uint8_t *)name;response.name_len=strlen(name);response.name_is_complete=1;
    if (ble_gap_adv_rsp_set_fields(&response)) return;
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
        .itvl_min=1280,.itvl_max=1600}; /* Device never scans; 0.8–1s advertising. */
    int rc=ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&p,gap,NULL);
    if (rc) ESP_LOGW(TAG,"Advertise failed: %d",rc);
}
static int access_gatt(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)attr;(void)arg;
    if (ctx->op==BLE_GATT_ACCESS_OP_READ_CHR && !ble_uuid_cmp(ctx->chr->uuid,&info_uuid.u))
        return os_mbuf_append(ctx->om,v_status.device_id,strlen(v_status.device_id))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    if (ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR || ble_uuid_cmp(ctx->chr->uuid,&rx_uuid.u)) return BLE_ATT_ERR_UNLIKELY;
    uint8_t frame[244];uint16_t length=0;
    if (OS_MBUF_PKTLEN(ctx->om)>sizeof(frame) || ble_hs_mbuf_to_flat(ctx->om,frame,sizeof(frame),&length)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    xSemaphoreTake(lock,portMAX_DELAY);slot_t *s=ensure_slot(conn);int result=-1;
    if (s) {
        result=vibe_ble_frame_add(&s->rx,frame,length,vibe_now());
        if (result==1) {
            ESP_LOGD(TAG,"Control received: %u bytes",(unsigned)s->rx.size);
            if (s->rx.bytes[0]==5) {
                /* Audio uses a separate bounded FIFO, never the control queue. */
                (void)vibe_relay_receive(s->route,s->rx.bytes,s->rx.size);
            } else if (s->rx.bytes[0]!=2 || !vibe_json_depth_ok((char *)s->rx.bytes+1,s->rx.size-1) ||
                !vibe_message_post_from(v_queue,(char *)s->rx.bytes+1,s->rx.size-1,s->route)) result=-1;
            vibe_ble_frame_clear(&s->rx);
        }
    }
    xSemaphoreGive(lock);
    return result<0?BLE_ATT_ERR_INSUFFICIENT_RES:0;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&info_uuid.u,.access_cb=access_gatt,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&rx_uuid.u,.access_cb=access_gatt,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC},
        {.uuid=&tx_uuid.u,.access_cb=access_gatt,.flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&tx_handle}, {0}}}, {0}};
static int gap(struct ble_gap_event *e,void *arg) {
    (void)arg;
    /* NOTIFY_TX can be synchronous. Only slot mutations need the state lock. */
    switch (e->type) {
        case BLE_GAP_EVENT_CONNECT:case BLE_GAP_EVENT_DISCONNECT:
        case BLE_GAP_EVENT_SUBSCRIBE:case BLE_GAP_EVENT_REPEAT_PAIRING:
        case BLE_GAP_EVENT_ENC_CHANGE:case BLE_GAP_EVENT_ADV_COMPLETE:break;
        default:return 0;
    }
    if (e->type==BLE_GAP_EVENT_REPEAT_PAIRING) {
        struct ble_gap_conn_desc d;
        if (!ble_gap_conn_find(e->repeat_pairing.conn_handle,&d)) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    xSemaphoreTake(lock,portMAX_DELAY);
    if (e->type==BLE_GAP_EVENT_CONNECT && !e->connect.status) {
        ESP_LOGI(TAG,"Connected handle=%u",e->connect.conn_handle);
        (void)ensure_slot(e->connect.conn_handle);
    } else if (e->type==BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI(TAG,"Disconnected handle=%u reason=%d",e->disconnect.conn.conn_handle,e->disconnect.reason);
        slot_t *s=find(e->disconnect.conn.conn_handle);
        if (s) forget_slot(s);
    } else if (e->type==BLE_GAP_EVENT_SUBSCRIBE && e->subscribe.attr_handle==tx_handle) {
        slot_t *s=e->subscribe.cur_notify?ensure_slot(e->subscribe.conn_handle):find(e->subscribe.conn_handle);
        if (s) s->subscribed=e->subscribe.cur_notify;
    } else if (e->type==BLE_GAP_EVENT_ENC_CHANGE) {
        ESP_LOGI(TAG,"Encryption handle=%u status=%d",e->enc_change.conn_handle,e->enc_change.status);
    }
    xSemaphoreGive(lock);advertise();return 0;
}
static void sync_host(void) {
    /* A persistent application-specific static identity avoids reusing an old
     * official/demo service's system bond. Never rotate it on boot or handoff.
     * The public factory device ID in GATT remains the cross-link identity. */
    if (ble_hs_id_set_rnd(identity)) { ESP_LOGE(TAG,"No BLE address");return; }
    address_type=BLE_OWN_ADDR_RANDOM;
    int count=0;(void)ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC,&count);
    ESP_LOGI(TAG,"Host ready, stored bonds=%d",count);
    xSemaphoreTake(lock,portMAX_DELAY);synced=true;xSemaphoreGive(lock);advertise();
}
static void reset_host(int reason) {
    xSemaphoreTake(lock,portMAX_DELAY);synced=false;
    for (unsigned i=0;i<2;i++) forget_slot(&slots[i]);
    xSemaphoreGive(lock);ESP_LOGE(TAG,"Host reset: %d",reason);
}
static void host_task(void *arg) {
    (void)arg;nimble_port_run();ESP_LOGI(TAG,"Host event loop stopped");xSemaphoreGive(host_done);vTaskDelete(NULL);
}
static esp_err_t start_stack_awake(void) {
    esp_err_t err=nimble_port_init();if (err!=ESP_OK) return err;
    ble_hs_cfg.sync_cb=sync_host;ble_hs_cfg.reset_cb=reset_host;ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) { nimble_port_deinit();return ESP_FAIL; }
    char name[24];snprintf(name,sizeof(name),"Vibe-%.6s",v_status.device_id+6);ble_svc_gap_device_name_set(name);
    ble_store_config_init();wanted=true;started=true;
    if (xTaskCreate(host_task,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,NULL,configMAX_PRIORITIES-4,NULL)!=pdPASS) {
        started=false;nimble_port_deinit();return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
static esp_err_t start_stack(void) {
    esp_pm_lock_acquire(transition_lock);
    esp_err_t err=start_stack_awake();
    esp_pm_lock_release(transition_lock);return err;
}
esp_err_t vibe_ble_init(void) {
    nvs_handle_t n;esp_err_t err=nvs_open("vibe",NVS_READWRITE,&n);if (err!=ESP_OK) return err;
    size_t size=sizeof(identity);err=nvs_get_blob(n,"ble_identity",identity,&size);
    if (err==ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(identity,sizeof(identity));identity[5]=(identity[5]&0x3f)|0xc0;
        err=nvs_set_blob(n,"ble_identity",identity,sizeof(identity));if (err==ESP_OK) err=nvs_commit(n);
    } else if (err==ESP_OK && (size!=sizeof(identity) || (identity[5]&0xc0)!=0xc0)) err=ESP_ERR_INVALID_STATE;
    nvs_close(n);if (err!=ESP_OK) return err;
    lock=xSemaphoreCreateMutex();tx_lock=xSemaphoreCreateMutex();host_done=xSemaphoreCreateBinary();if (!lock || !tx_lock || !host_done) return ESP_ERR_NO_MEM;
    err=esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"vibe_ble_switch",&transition_lock);if (err!=ESP_OK) return err;
    /* Prepare synchronization before starting the network worker, but let that
     * worker start the host only after Wi-Fi's resident allocations exist.
     * Group lifetime allocations before optional pools, and keep all stack
     * transitions in the worker that also owns enable/disable at runtime.
     * Ordering alone does not guarantee enough contiguous RAM for a decoder. */
    wanted=v_status.ble_enabled;return ESP_OK;
}
bool vibe_ble_enable(bool enabled) {
    xSemaphoreTake(tx_lock,portMAX_DELAY);
    if (enabled==started) { xSemaphoreGive(tx_lock);return started; }
    if (enabled) {
        if (vibe_now()<retry_after) { xSemaphoreGive(tx_lock);return started; }
        esp_err_t err=start_stack();if (err!=ESP_OK) {
            retry_after=vibe_now()+30000;ESP_LOGE(TAG,"Start failed: %s",esp_err_to_name(err));
        }
        xSemaphoreGive(tx_lock);return started;
    }
    esp_pm_lock_acquire(transition_lock);
    xSemaphoreTake(lock,portMAX_DELAY);wanted=false;started=false;xSemaphoreGive(lock);
    ble_gap_adv_stop();
    /* Stop/join/deinit in the network worker, never in a key or GAP callback.
     * No irreversible bt_mem_release: enabling again in the same boot works. */
    ESP_LOGI(TAG,"Stopping host");
    int rc=nimble_port_stop();
    ESP_LOGI(TAG,"Host stop returned: %d",rc);
    if (!rc) {
        xSemaphoreTake(host_done,portMAX_DELAY);
        ESP_LOGI(TAG,"Deinitializing stack");
        esp_err_t err=nimble_port_deinit();
        ESP_LOGI(TAG,"Stack deinit returned: %s",esp_err_to_name(err));
        xSemaphoreTake(lock,portMAX_DELAY);synced=false;
        for (unsigned i=0;i<2;i++) forget_slot(&slots[i]);
        xSemaphoreGive(lock);
    } else { xSemaphoreTake(lock,portMAX_DELAY);wanted=true;started=true;xSemaphoreGive(lock);ESP_LOGE(TAG,"Stop failed: %d",rc); }
    esp_pm_lock_release(transition_lock);
    xSemaphoreGive(tx_lock);return started;
}
void vibe_ble_poll(uint64_t now,bool voice,vibe_route_t keep) {
    /* Only the network worker polls/toggles the stack. Capture slot decisions,
     * release state, then call the host. A host callback may run immediately. */
    for (unsigned i=0;i<2;i++) {
        xSemaphoreTake(lock,portMAX_DELAY);
        if (!started) { xSemaphoreGive(lock);return; }
        slot_t *s=&slots[i];vibe_route_t route=s->route;
        bool mine=s->used && vibe_route_equal(route,keep);
        bool expired=s->used && !mine && now-s->opened>30000;
        if (s->rx.bytes && now-s->rx.at>2000) vibe_ble_frame_clear(&s->rx);
        bool fast=mine && voice,update=s->used && !expired && (fast!=s->fast || !s->parameters_set);
        xSemaphoreGive(lock);
        if (expired) ble_gap_terminate(route.handle,BLE_ERR_REM_USER_CONN_TERM);
        if (update) {
            struct ble_gap_upd_params p={.itvl_min=12,.itvl_max=24,.latency=fast?0:29,.supervision_timeout=600};
            if (!ble_gap_update_params(route.handle,&p)) {
                xSemaphoreTake(lock,portMAX_DELAY);s=find(route.handle);
                if (s && vibe_route_equal(s->route,route)) { s->fast=fast;s->parameters_set=true; }
                xSemaphoreGive(lock);
            }
        }
    }
    advertise();
}
int vibe_ble_send(vibe_route_t route,unsigned char type,const void *bytes,size_t size) {
    if (size+1>VIBE_BLE_PACKET_MAX) return -1;
    xSemaphoreTake(tx_lock,portMAX_DELAY);
    xSemaphoreTake(lock,portMAX_DELAY);slot_t *s=find(route.handle);
    bool valid=started && s && vibe_route_equal(s->route,route) && s->subscribed;
    xSemaphoreGive(lock);
    if (!valid) { xSemaphoreGive(tx_lock);return -1; }
    unsigned mtu=ble_att_mtu(route.handle);
    if (mtu<23) { xSemaphoreGive(tx_lock);return -1; }
    unsigned capacity=mtu>247?244:mtu-3;
    if (capacity<=VIBE_BLE_HEADER) { xSemaphoreGive(tx_lock);return -1; }
    uint16_t id=++message_id,total=(uint16_t)(size+1);int result=0;
    const uint64_t deadline=vibe_now()+(type==1?80:1000);
    for (unsigned off=0;off<total;) {
        uint8_t frame[244];unsigned chunk=total-off;if (chunk>capacity-VIBE_BLE_HEADER) chunk=capacity-VIBE_BLE_HEADER;
        vibe_ble_frame_header(frame,id,off,total);
        if (!off) { frame[6]=type;if (chunk>1) memcpy(frame+7,bytes,chunk-1); }
        else memcpy(frame+6,(const uint8_t *)bytes+off-1,chunk);
        for (;;) {
            xSemaphoreTake(lock,portMAX_DELAY);s=find(route.handle);
            valid=started && s && s->subscribed && vibe_route_equal(s->route,route);
            xSemaphoreGive(lock);
            if (!valid) { result=-1;break; }
            struct os_mbuf *om=ble_hs_mbuf_from_flat(frame,chunk+6);
            int rc=om?ble_gatts_notify_custom(route.handle,tx_handle,om):BLE_HS_ENOMEM;
            if (!rc) break; /* NimBLE consumes om on success and failure. */
            if ((rc!=BLE_HS_ENOMEM && rc!=BLE_HS_EAGAIN && rc!=BLE_HS_EBUSY) || vibe_now()>=deadline) { result=-1;break; }
            /* HCI queues are bounded, not an unlimited packet FIFO. Let the
             * host/controller drain before retrying this same fragment. */
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (result) break;
        off+=chunk;
    }
    if (result) ESP_LOGW(TAG,"Packet could not be sent: type=%u bytes=%u",type,(unsigned)size);
    xSemaphoreGive(tx_lock);return result;
}
