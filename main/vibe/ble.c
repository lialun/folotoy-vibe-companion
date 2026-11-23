#include "vibe.h"
#include "ble.h"
#include "ble_frame.h"
#include "ble_links.h"
#include "ble_tx_budget.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include "os/os_mempool.h"
#include "os/os.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

/* 71DF1000/1/2/3-7B51-4A1D-A9B3-5A7669626532, little-endian. */
#define UUID(x) BLE_UUID128_INIT(0x32,0x65,0x62,0x69,0x76,0x5a,0xb3,0xa9,0x1d,0x4a,0x51,0x7b,x,0x10,0xdf,0x71)
static const ble_uuid128_t service_uuid=UUID(0x00),info_uuid=UUID(0x01),rx_uuid=UUID(0x02),tx_uuid=UUID(0x03);
#define LINK_SLOTS VIBE_BLE_LINK_SLOTS
typedef vibe_ble_link_t ble_link_t;
static vibe_ble_links_t registry;
#define links registry.links
static SemaphoreHandle_t links_mutex,metadata_mutex;
static char metadata[512];
static size_t metadata_length;
static ble_link_t *find_link(uint16_t handle) { return vibe_ble_link_find(&registry,handle); }
static uint16_t tx_handle;
static SemaphoreHandle_t tx_mutex;
static uint8_t address_type,message_id;
static atomic_bool ready;
static bool advertising_connectable;
static struct ble_npl_callout parameter_callout;
void ble_store_config_init(void);
static void advertise(void);
void vibe_ble_publish_metadata(const char *text,size_t length) {
    if (!metadata_mutex || length>=sizeof(metadata)) return;
    xSemaphoreTake(metadata_mutex,portMAX_DELAY);memcpy(metadata,text,length);metadata_length=length;xSemaphoreGive(metadata_mutex);
}
static bool secure(uint16_t handle) {
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(handle,&desc)==0 && desc.sec_state.encrypted;
}
bool vibe_ble_generation_matches(uint16_t handle,uint32_t generation) {
    xSemaphoreTake(links_mutex,portMAX_DELAY);ble_link_t *link=find_link(handle);
    bool current=link && link->generation==generation;xSemaphoreGive(links_mutex);return current;
}
uint32_t vibe_ble_generation(uint16_t handle) {
    xSemaphoreTake(links_mutex,portMAX_DELAY);ble_link_t *link=find_link(handle);
    uint32_t generation=link?link->generation:0;xSemaphoreGive(links_mutex);return generation;
}
static int access_cb(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)attr;
    if (arg) {
        char text[512];
        /* Never acquire an application/session mutex while inside the host.
         * Network sends can already hold those locks while waiting for NimBLE. */
        xSemaphoreTake(metadata_mutex,portMAX_DELAY);size_t n=metadata_length;memcpy(text,metadata,n);xSemaphoreGive(metadata_mutex);
        return n>0 && os_mbuf_append(ctx->om,text,n)==0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (!secure(conn)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint16_t n=OS_MBUF_PKTLEN(ctx->om);uint8_t frame[512];
    if (n>sizeof(frame) || ble_hs_mbuf_to_flat(ctx->om,frame,sizeof(frame),NULL)!=0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    ble_link_t *link=find_link(conn);
    if (!link) return BLE_ATT_ERR_UNLIKELY;
    uint64_t now=vibe_now();vibe_ble_media_expire(&link->media,now);
    if(n>7 && (frame[2]&4)) {
        vibe_ble_frame_t *media=vibe_ble_media_feed(&link->media,frame,n,now);
        if(media) {vibe_receive_ble(conn,link->generation,media->bytes,media->used);vibe_ble_media_clear(&link->media);}
    } else if (vibe_ble_frame_feed(&link->incoming,frame,n,now)) vibe_receive_ble(conn,link->generation,link->incoming.bytes,link->incoming.used);
    return 0;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&info_uuid.u,.access_cb=access_cb,.arg=(void *)1,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&rx_uuid.u,.access_cb=access_cb,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC},
        {.uuid=&tx_uuid.u,.val_handle=&tx_handle,.access_cb=access_cb,.flags=BLE_GATT_CHR_F_NOTIFY},
        {0}}}, {0}
};
/* The NimBLE host owns these policies and callbacks. Never hold an application
 * mutex across a GAP/HCI call. Only a pending negotiation arms the callout. */
static void parameter_schedule(void) {
    uint64_t now=vibe_now(),due=UINT64_MAX;
    for(unsigned i=0;i<LINK_SLOTS;i++) if(links[i].handle!=BLE_HS_CONN_HANDLE_NONE && links[i].parameters.due && links[i].parameters.due<due) due=links[i].parameters.due;
    if(due==UINT64_MAX) {ble_npl_callout_stop(&parameter_callout);return;}
    uint32_t delay=due>now?(uint32_t)(due-now):1;
    ble_npl_callout_reset(&parameter_callout,ble_npl_time_ms_to_ticks32(delay));
}
static void parameter_update_event(struct ble_npl_event *event) {
    (void)event;uint64_t now=vibe_now();
    for(unsigned i=0;i<LINK_SLOTS;i++) {
        ble_link_t *link=&links[i];struct ble_gap_conn_desc desc;
        if(link->handle==BLE_HS_CONN_HANDLE_NONE || !link->parameters.due) continue;
        if(ble_gap_conn_find(link->handle,&desc)) {link->parameters.due=0;continue;}
        if(desc.supervision_timeout>=VIBE_BLE_MIN_SUPERVISION) {vibe_ble_parameters_observe(&link->parameters,now,desc.supervision_timeout);continue;}
        if(!vibe_ble_parameters_due(&link->parameters,now)) continue;
        struct ble_gap_upd_params params={.itvl_min=12,.itvl_max=24,.latency=0,.supervision_timeout=VIBE_BLE_SUPERVISION};
        int result=ble_gap_update_params(link->handle,&params);
        if(result) {vibe_ble_parameters_observe(&link->parameters,now,desc.supervision_timeout);ESP_LOGW("vibe_ble","Parameter request deferred: result=%d attempt=%u",result,link->parameters.attempts);}
    }
    parameter_schedule();
}
static int gap_event(struct ble_gap_event *event,void *arg) {
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!event->connect.status) {
            uint16_t handle=event->connect.conn_handle;
            xSemaphoreTake(links_mutex,portMAX_DELAY);
            /* Bond restoration can deliver ENC_CHANGE/SUBSCRIBE before CONNECT.
             * Preserve that state instead of creating/resetting a duplicate slot. */
            (void)vibe_ble_link_ensure(&registry,handle);
            xSemaphoreGive(links_mutex);
            ble_link_t *link=find_link(handle);
            if(link) vibe_ble_parameters_start(&link->parameters,vibe_now());
            parameter_schedule();
            /* Keep the initial PHY/DLE procedures with the stack. Starting
             * another PHY procedure here collided with parameter negotiation. */
            int security=secure(handle)?BLE_HS_EALREADY:ble_gap_security_initiate(handle);
            ESP_LOGI("vibe_ble","Connected handle=%u security_request=%d",(unsigned)handle,security);
        }
        advertise();break;
    case BLE_GAP_EVENT_DISCONNECT: {
        uint16_t handle=event->disconnect.conn.conn_handle;
        ESP_LOGI("vibe_ble","Disconnected handle=%u reason=%d",(unsigned)handle,event->disconnect.reason);
        xSemaphoreTake(links_mutex,portMAX_DELAY);ble_link_t *link=find_link(handle);
        if (link) vibe_ble_link_remove(&registry,handle);
        xSemaphoreGive(links_mutex);parameter_schedule();vibe_ble_disconnected(handle);
        advertise();break;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: {
        ble_link_t *link=find_link(event->conn_update.conn_handle);struct ble_gap_conn_desc desc;
        if(link && !ble_gap_conn_find(link->handle,&desc)) {
            vibe_ble_parameters_observe(&link->parameters,vibe_now(),desc.supervision_timeout);
            ESP_LOGI("vibe_ble","Connection parameters: status=%d interval=%u timeout=%u attempt=%u",event->conn_update.status,desc.conn_itvl,desc.supervision_timeout,link->parameters.attempts);
        }
        parameter_schedule();break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI("vibe_ble","Encryption handle=%u status=%d secure=%d",(unsigned)event->enc_change.conn_handle,event->enc_change.status,secure(event->enc_change.conn_handle));break;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        xSemaphoreTake(links_mutex,portMAX_DELAY);
        vibe_ble_link_subscription(&registry,event->subscribe.conn_handle,event->subscribe.attr_handle,tx_handle,event->subscribe.cur_notify);
        xSemaphoreGive(links_mutex);
        ESP_LOGI("vibe_ble","Notify subscription handle=%u attr=%u tx=%u active=%u",(unsigned)event->subscribe.conn_handle,(unsigned)event->subscribe.attr_handle,(unsigned)tx_handle,(unsigned)event->subscribe.cur_notify);break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* The app-level credential and physical confirmation authorize control.
         * Replacing a stale OS bond must not require a second numeric code. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle,&desc)==0) ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise();break;
    default:break;
    }
    return 0;
}
static void advertise(void) {
    if (!ready) return;
    bool available=false;
    xSemaphoreTake(links_mutex,portMAX_DELAY);
    for (unsigned i=0;i<LINK_SLOTS;i++) if (links[i].handle==BLE_HS_CONN_HANDLE_NONE) available=true;
    xSemaphoreGive(links_mutex);
    /* Keep an existing advertisement unless slot capacity changes its mode. */
    if (ble_gap_adv_active()) {
        if (advertising_connectable==available) return;
        if (ble_gap_adv_stop()!=0) return;
    }
    struct ble_hs_adv_fields fields={.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128=(ble_uuid128_t *)&service_uuid,.num_uuids128=1,.uuids128_is_complete=1};
    if (ble_gap_adv_set_fields(&fields)!=0) return;
    const char *name=ble_svc_gap_device_name();
    struct ble_hs_adv_fields response={.name=(uint8_t *)name,.name_len=strlen(name),.name_is_complete=1};
    if (ble_gap_adv_rsp_set_fields(&response)!=0) return;
    struct ble_gap_adv_params params={.conn_mode=available ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON,
        .disc_mode=BLE_GAP_DISC_MODE_GEN,.itvl_min=800,.itvl_max=1200};
    if (ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL)==0) advertising_connectable=available;
}
static void on_reset(int reason) {
    (void)reason;ready=false;ble_npl_callout_stop(&parameter_callout);
    xSemaphoreTake(links_mutex,portMAX_DELAY);
    for (unsigned i=0;i<LINK_SLOTS;i++) {
        if (links[i].handle!=BLE_HS_CONN_HANDLE_NONE) vibe_ble_disconnected(links[i].handle);
        vibe_ble_media_clear(&links[i].media);links[i].handle=BLE_HS_CONN_HANDLE_NONE;links[i].subscribed=false;
    }
    xSemaphoreGive(links_mutex);
}
static void on_sync(void) {
    if (ble_hs_id_infer_auto(0,&address_type)!=0) return;
    /* Preserve bonds while invalidating the host's cached GATT properties.
     * In particular, older firmware advertised RX without Write Command.
     * NimBLE persists this indication for bonded peers not yet connected. */
    ble_svc_gatt_changed(1,UINT16_MAX);
    ready=true;advertise();
}
static void host_task(void *arg) { (void)arg;nimble_port_run();nimble_port_freertos_deinit(); }
esp_err_t vibe_ble_init(void) {
    /* Per-notification INFO output can flood the USB console during audio. */
    esp_log_level_set("NimBLE",ESP_LOG_WARN);
    tx_mutex=xSemaphoreCreateMutex();links_mutex=xSemaphoreCreateMutex();metadata_mutex=xSemaphoreCreateMutex();
    if (!tx_mutex || !links_mutex || !metadata_mutex) return ESP_ERR_NO_MEM;
    vibe_ble_links_init(&registry);
    char initial[512];int count=vibe_discovery_json(initial,sizeof(initial),false,false);
    if (count>0) vibe_ble_publish_metadata(initial,count);
    esp_err_t error=nimble_port_init();if (error!=ESP_OK) return error;
    ble_npl_callout_init(&parameter_callout,nimble_port_get_dflt_eventq(),parameter_update_event,NULL);
    ble_hs_cfg.sync_cb=on_sync;ble_hs_cfg.reset_cb=on_reset;ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_bonding=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();ble_store_config_init();
    char name[24];snprintf(name,sizeof(name),"Vibe-%s",v_status.device_id);ble_svc_gap_device_name_set(name);
    if (ble_gatts_count_cfg(services)!=0 || ble_gatts_add_svcs(services)!=0) { nimble_port_deinit();return ESP_FAIL; }
    ble_att_set_preferred_mtu(247);nimble_port_freertos_init(host_task);return ESP_OK;
}
int vibe_ble_send_to(uint16_t conn,unsigned char type,const void *bytes,size_t length) {
    if (!tx_mutex || xSemaphoreTake(tx_mutex,0)!=pdTRUE) return -1;
    xSemaphoreTake(links_mutex,portMAX_DELAY);ble_link_t *link=find_link(conn);
    bool subscribed=link && link->subscribed;xSemaphoreGive(links_mutex);
    bool encrypted=secure(conn);
    if (!subscribed || !encrypted || length+1>VIBE_BLE_PACKET_MAX) {
        static unsigned dropped;
        if (dropped++<4) ESP_LOGW("vibe_ble","TX rejected handle=%u subscribed=%d secure=%d",(unsigned)conn,subscribed,encrypted);
        xSemaphoreGive(tx_mutex);return -1;
    }
    unsigned mtu=ble_att_mtu(conn),capacity=mtu>10 ? mtu-3-VIBE_BLE_HEADER : 0;
    if (!capacity) { xSemaphoreGive(tx_mutex);return -1; }
    if (capacity>240) capacity=240;
    uint8_t id=++message_id,frame[247];size_t total=length+1;
    const bool audio=type==1 || type==6;uint64_t began=vibe_now();
    for (size_t offset=0;offset<total;) {
        size_t n=total-offset;if (n>capacity) n=capacity;
        frame[0]=0x56;frame[1]=id;frame[2]=(offset==0?1:0)|(offset+n==total?2:0);
        frame[3]=total&255;frame[4]=total>>8;frame[5]=offset&255;frame[6]=offset>>8;
        for (size_t i=0;i<n;i++) frame[VIBE_BLE_HEADER+i]=(offset+i)==0 ? type : ((const uint8_t *)bytes)[offset+i-1];
        int result=BLE_HS_ENOMEM;unsigned free_total=0,min_pool=0;
        do {
            struct os_mbuf *buffer=ble_hs_mbuf_att_pkt();
            free_total=(unsigned)os_msys_num_free();min_pool=UINT16_MAX;
            if (buffer) {
                /* Append uses this same pool for the entire chain. Check its
                 * projected cost before copying; allocating the full chain
                 * first would temporarily consume the control reserve. */
                uint32_t critical;OS_ENTER_CRITICAL(critical);
                min_pool=buffer->om_omp->omp_pool->mp_num_free;
                OS_EXIT_CRITICAL(critical);
                unsigned space=OS_MBUF_TRAILINGSPACE(buffer),block=buffer->om_omp->omp_databuf_len;
                unsigned extra=n+VIBE_BLE_HEADER>space ? (n+VIBE_BLE_HEADER-space+block-1)/block:0;
                if (extra<=free_total && extra<=min_pool && vibe_ble_tx_admit(free_total-extra,min_pool-extra,audio) &&
                    os_mbuf_append(buffer,frame,n+VIBE_BLE_HEADER)==0) {
                    result=ble_gatts_notify_custom(conn,tx_handle,buffer); /* Consumes buffer on every outcome. */
                    if (!result || (result!=BLE_HS_ENOMEM && result!=BLE_HS_EAGAIN)) break;
                } else os_mbuf_free_chain(buffer);
            } else min_pool=0;
            if (!vibe_ble_tx_wait(began,vibe_now(),audio)) break;
            vTaskDelay(pdMS_TO_TICKS(2)?pdMS_TO_TICKS(2):1);
        } while (vibe_ble_tx_wait(began,vibe_now(),audio));
        if (result) {
            static uint64_t reported;static unsigned failures;++failures;
            uint64_t now=vibe_now();
            if (!reported || now-reported>=5000) {
                reported=now;ESP_LOGW("vibe_ble","TX pressure handle=%u type=%u result=%d free=%u pool=%u failures=%u heap=%u largest=%u",(unsigned)conn,type,result,free_total,min_pool,failures,(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            }
            xSemaphoreGive(tx_mutex);return -1;
        }
        offset+=n;
        /* The next fragment must pass the same pool admission. Wait only under
         * pressure; imposing a per-fragment delay would hurt small-MTU peers. */
    }
    xSemaphoreGive(tx_mutex);return 0;
}
