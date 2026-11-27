/* Network-worker-only portal lifetime. Readiness is published only after
 * every route exists; partial startup uses the same teardown as normal exit. */
static bool setup_ble_paused;
static uint64_t ble_resume_at;
static void setup_restore_ble(uint64_t now) {
    if (!setup_ble_paused) return;
    esp_err_t result=vibe_ble_init();ble_resume_at=now+2000;
    if (result==ESP_OK) {
        setup_ble_paused=false;ble_resume_at=0;
        xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.provisioning=false;xSemaphoreGive(v_mutex);
    } else ESP_LOGE(TAG,"Bluetooth restore deferred: %s",esp_err_to_name(result));
}
static void finish_ap(bool completed);
static void start_ap(void) {
    if (ap_started) {ui_provision(1);return;}
    setup_saved_at=0;
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    snprintf(v_status.ap_name,sizeof(v_status.ap_name),"Vibe-Setup-%.6s",v_status.device_id+6);
    setup_phase=SETUP_SELECT;
    strlcpy(v_status.notice,"选择 Wi-Fi",sizeof(v_status.notice));
    wifi_config_t ap={0};
    strlcpy((char *)ap.ap.ssid,v_status.ap_name,sizeof(ap.ap.ssid));
    ap.ap.ssid_len=strlen(v_status.ap_name);ap.ap.channel=1;ap.ap.max_connection=2;
    ap.ap.authmode=WIFI_AUTH_OPEN;v_status.provisioning=true;
    bool connected=v_status.wifi;
    xSemaphoreGive(v_mutex);
    auto_connection.pending=false;
    if (!connected) esp_wifi_disconnect();
    /* Set before driver changes so any partial failure has a complete rollback. */
    ap_started=true;
    /* Provisioning has no voice/radio interaction. Release the Bluetooth
     * stack before allocating AP/TCP resources; its credentials remain in NVS. */
    setup_ble_paused=true;
    esp_err_t error=setup_pause_ble();if (error!=ESP_OK) goto failed;
    error=esp_wifi_set_mode(WIFI_MODE_APSTA);if (error!=ESP_OK) goto failed;
    error=esp_wifi_set_config(WIFI_IF_AP,&ap);if (error!=ESP_OK) goto failed;
    if (!wifi_started) {
        error=esp_wifi_start();if (error!=ESP_OK) goto failed;
        wifi_started=true;
    }
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    /* Status reads copy only their small response, not the whole device model.
     * Two LRU sessions fit the page's two parallel reads while bounding phone
     * background probes and their TCP buffers. Media retains its fixed reserve. */
    config.stack_size=4096;config.max_uri_handlers=6;config.max_open_sockets=2;config.lru_purge_enable=true;
    config.recv_wait_timeout=3;config.send_wait_timeout=3;
    error=httpd_start(&web,&config);if (error!=ESP_OK) goto failed;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=setup_page},
        {.uri="/status",.method=HTTP_GET,.handler=setup_status},
        {.uri="/scan",.method=HTTP_GET,.handler=setup_scan},
        {.uri="/saved",.method=HTTP_GET,.handler=setup_saved},
        {.uri="/forget",.method=HTTP_POST,.handler=setup_forget},
        {.uri="/save",.method=HTTP_POST,.handler=setup_save}};
    for (size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) {
        error=httpd_register_uri_handler(web,&routes[i]);if (error!=ESP_OK) goto failed;
    }
    error=httpd_register_err_handler(web,HTTPD_404_NOT_FOUND,portal_redirect);
    if (error!=ESP_OK) goto failed;
    portal_dns_start();ui_provision(1);
    ESP_LOGI(TAG,"Setup ready: heap=%u largest=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return;
failed:
    ESP_LOGE(TAG,"Setup startup failed: %s heap=%u largest=%u",esp_err_to_name(error),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    finish_ap(false);
}
static void finish_ap(bool completed) {
    if (!ap_started) return;
    setup_saved_at=0;
    if (dns_socket>=0) {close(dns_socket);dns_socket=-1;}
    if (web) {httpd_stop(web);web=NULL;}
    esp_wifi_set_mode(WIFI_MODE_STA);ap_started=false;
    xSemaphoreTake(profiles_mutex,portMAX_DELAY);bool has_saved=networks.count!=0;xSemaphoreGive(profiles_mutex);
    if (!has_saved) {esp_wifi_stop();wifi_started=false;}
    vibe_wifi_retry_reset(&auto_connection,vibe_now());
    if (setup_ble_paused) setup_restore_ble(vibe_now());
    else {xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.provisioning=false;xSemaphoreGive(v_mutex);}
    ui_provision(completed?2:0);
}
