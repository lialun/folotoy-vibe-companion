#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/semphr.h"
#include "esp_err.h"
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define configMAX_PRIORITIES 25
#define ESP_ERR_NVS_NOT_FOUND 0x1102
typedef struct { uint8_t value[16]; } ble_uuid_t;
typedef struct { ble_uuid_t u; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) { .u={.value={__VA_ARGS__}} }
enum { BLE_HS_ENOMEM=6,BLE_HS_EAGAIN=1,BLE_HS_EBUSY=15,BLE_HS_ADV_F_DISC_GEN=1,BLE_HS_ADV_F_BREDR_UNSUP=2,BLE_GAP_CONN_MODE_UND=3,
    BLE_GAP_DISC_MODE_GEN=4,BLE_HS_FOREVER=5,BLE_GATT_ACCESS_OP_READ_CHR=6,
    BLE_GATT_ACCESS_OP_WRITE_CHR=7,BLE_ATT_ERR_INSUFFICIENT_RES=8,
    BLE_ATT_ERR_UNLIKELY=9,BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN=10,
    BLE_GATT_SVC_TYPE_PRIMARY=11,BLE_GATT_CHR_F_READ=1,BLE_GATT_CHR_F_WRITE=2,
    BLE_GATT_CHR_F_WRITE_NO_RSP=16,BLE_GATT_CHR_F_WRITE_ENC=4,BLE_GATT_CHR_F_NOTIFY=8,
    BLE_GAP_EVENT_CONNECT=12,BLE_GAP_EVENT_DISCONNECT=13,BLE_GAP_EVENT_SUBSCRIBE=14,
    BLE_GAP_EVENT_REPEAT_PAIRING=15,BLE_GAP_EVENT_ENC_CHANGE=16,
    BLE_GAP_EVENT_ADV_COMPLETE=17,BLE_GAP_EVENT_NOTIFY_TX=18,
    BLE_GAP_REPEAT_PAIRING_RETRY=19,BLE_OWN_ADDR_RANDOM=20,BLE_STORE_OBJ_TYPE_OUR_SEC=21,
    BLE_HS_IO_NO_INPUT_OUTPUT=22,BLE_SM_PAIR_KEY_DIST_ENC=1,BLE_SM_PAIR_KEY_DIST_ID=2,
    BLE_ERR_REM_USER_CONN_TERM=23 };
struct os_mbuf { unsigned length;uint8_t bytes[244]; };
#define OS_MBUF_PKTLEN(p) ((p)->length)
struct ble_gap_event {
    int type;
    struct { int status;uint16_t conn_handle; } connect,enc_change;
    struct { struct { uint16_t conn_handle; } conn;int reason; } disconnect;
    struct { uint16_t conn_handle,attr_handle;bool cur_notify; } subscribe;
    struct { uint16_t conn_handle; } repeat_pairing;
};
struct ble_gap_conn_desc { unsigned peer_id_addr; };
struct ble_gap_upd_params { unsigned itvl_min,itvl_max,latency,supervision_timeout; };
struct ble_hs_cfg_stub { void (*sync_cb)(void);void (*reset_cb)(int);int (*store_status_cb)(void *);
    int sm_io_cap,sm_bonding,sm_sc,sm_our_key_dist,sm_their_key_dist; };
extern struct ble_hs_cfg_stub ble_hs_cfg;
struct ble_gatt_chr_def;
struct ble_gatt_access_ctxt { int op;const struct ble_gatt_chr_def *chr;struct os_mbuf *om; };
struct ble_gatt_chr_def { const ble_uuid_t *uuid;int (*access_cb)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);unsigned flags;uint16_t *val_handle; };
struct ble_gatt_svc_def { int type;const ble_uuid_t *uuid;const struct ble_gatt_chr_def *characteristics; };
struct ble_hs_adv_fields { unsigned flags;ble_uuid128_t *uuids128;unsigned num_uuids128;bool uuids128_is_complete;
    uint8_t *name;unsigned name_len;bool name_is_complete; };
struct ble_gap_adv_params { unsigned conn_mode,disc_mode,itvl_min,itvl_max; };
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int ble_gap_adv_active(void);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_start(uint8_t,const void *,int,const struct ble_gap_adv_params *,int (*)(struct ble_gap_event *,void *),void *);
int ble_gap_adv_stop(void);
const char *ble_svc_gap_device_name(void);
int os_mbuf_append(struct os_mbuf *,const void *,unsigned);
int ble_uuid_cmp(const ble_uuid_t *,const ble_uuid_t *);
int ble_hs_mbuf_to_flat(struct os_mbuf *,void *,unsigned,uint16_t *);
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_store_util_delete_peer(const void *);
int ble_hs_id_set_rnd(const uint8_t *);
int ble_store_util_count(int,int *);
void nimble_port_run(void);
esp_err_t nimble_port_init(void);
esp_err_t nimble_port_deinit(void);
int nimble_port_stop(void);
int ble_store_util_status_rr(void *);
void ble_svc_gap_init(void);
void ble_svc_gatt_init(void);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
int ble_svc_gap_device_name_set(const char *);
int ble_gap_terminate(uint16_t,int);
int ble_gap_update_params(uint16_t,const struct ble_gap_upd_params *);
unsigned ble_att_mtu(uint16_t);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *,unsigned);
int ble_gatts_notify_custom(uint16_t,uint16_t,struct os_mbuf *);
void esp_fill_random(void *,size_t);
