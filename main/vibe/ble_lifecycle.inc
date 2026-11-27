/* Boot initialization and subsequent transitions are serialized by the network
 * worker. The host owns GAP callbacks; TX admission closes before it is stopped.
 * No OS bonds, application credentials or media reservations are erased. */
static bool ble_prepared,stack_initialized,host_running,stack_stopping,stack_faulted;
static _Atomic(TaskHandle_t) stop_waiter;
static void host_task(void *arg) {
    (void)arg;nimble_port_run();
    TaskHandle_t waiter=atomic_load(&stop_waiter);
    if (waiter) xTaskNotifyGive(waiter);
    vTaskDelete(NULL);
}
static void release_npl(void) {
    if (!atomic_exchange(&npl_initialized,false)) return;
    ble_npl_callout_deinit(&parameter_callout);
    ble_npl_event_deinit(&streaming_event);ble_npl_event_deinit(&radio_clear_event);
    memset(&parameter_callout,0,sizeof(parameter_callout));
    memset(&streaming_event,0,sizeof(streaming_event));memset(&radio_clear_event,0,sizeof(radio_clear_event));
}
static esp_err_t dispose_stack(void) {
    release_npl();
    esp_err_t result=nimble_port_deinit();
    if (result!=ESP_OK) {stack_faulted=true;return result;}
    stack_initialized=false;stack_stopping=false;atomic_store(&stop_waiter,NULL);
    return ESP_OK;
}
esp_err_t vibe_ble_pause(void);
esp_err_t vibe_ble_init(void) {
    if (stack_faulted) return ESP_ERR_INVALID_STATE;
    if (stack_stopping) {
        esp_err_t result=vibe_ble_pause();if (result!=ESP_OK) return result;
    }
    if (stack_initialized) return ESP_OK;
    esp_log_level_set("NimBLE",ESP_LOG_WARN);
    if (!tx_mutex) tx_mutex=xSemaphoreCreateMutex();
    if (!links_mutex) links_mutex=xSemaphoreCreateMutex();
    if (!metadata_mutex) metadata_mutex=xSemaphoreCreateMutex();
    if (!tx_mutex || !links_mutex || !metadata_mutex) return ESP_ERR_NO_MEM;
    if (!ble_prepared) {vibe_ble_links_init(&registry);ble_prepared=true;}
    char initial[512];int count=vibe_discovery_json(initial,sizeof(initial),false,false,false);
    if (count>0) vibe_ble_publish_metadata(initial,count);
    esp_err_t error=nimble_port_init();if (error!=ESP_OK) return error;
    stack_initialized=true;
    ble_npl_callout_init(&parameter_callout,nimble_port_get_dflt_eventq(),parameter_update_event,NULL);
    ble_npl_event_init(&streaming_event,streaming_update_event,NULL);
    ble_npl_event_init(&radio_clear_event,radio_clear,NULL);npl_initialized=true;
    ble_hs_cfg.sync_cb=on_sync;ble_hs_cfg.reset_cb=on_reset;ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_bonding=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();ble_store_config_init();
    char name[24];snprintf(name,sizeof(name),"Vibe-%s",v_status.device_id);ble_svc_gap_device_name_set(name);
    if (ble_gatts_count_cfg(services)!=0 || ble_gatts_add_svcs(services)!=0) {
        (void)dispose_stack();return ESP_FAIL;
    }
    ble_att_set_preferred_mtu(247);ble_enabled=true;
    if (xTaskCreatePinnedToCore(host_task,"nimble_host",NIMBLE_HS_STACK_SIZE,NULL,
            configMAX_PRIORITIES-4,NULL,NIMBLE_CORE)!=pdPASS) {
        ble_enabled=false;(void)dispose_stack();return ESP_ERR_NO_MEM;
    }
    host_running=true;return ESP_OK;
}
esp_err_t vibe_ble_pause(void) {
    if (stack_faulted) return ESP_ERR_INVALID_STATE;
    if (!stack_initialized) return ESP_OK;
    if (!stack_stopping) {
        xSemaphoreTake(tx_mutex,portMAX_DELAY);
        bool was_ready=ready;ble_enabled=false;ready=false;
        xSemaphoreGive(tx_mutex);
        atomic_store(&stop_waiter,xTaskGetCurrentTaskHandle());
        (void)ulTaskNotifyTake(pdTRUE,0);
        int result=nimble_port_stop();
        if (result) {
            atomic_store(&stop_waiter,NULL);ble_enabled=true;ready=was_ready;
            return ESP_FAIL;
        }
        stack_stopping=true;
    }
    if (host_running) {
        if (!ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2000))) return ESP_ERR_TIMEOUT;
        host_running=false;
    }
    esp_err_t error=dispose_stack();if (error!=ESP_OK) return error;
    xSemaphoreTake(tx_mutex,portMAX_DELAY);
    ready=false;voice_cursor.active=false;voice_probe_handle=UINT16_MAX;memset(&att_layout,0,sizeof(att_layout));
    xSemaphoreTake(links_mutex,portMAX_DELAY);
    for (unsigned i=0;i<LINK_SLOTS;i++) vibe_ble_link_remove(&registry,links[i].handle);
    xSemaphoreGive(links_mutex);xSemaphoreGive(tx_mutex);
    /* The stopped higher-priority host has deleted itself. Let the idle task
     * reclaim its stack before provisioning consumes the recovered memory. */
    vTaskDelay(1);
    return ESP_OK;
}
