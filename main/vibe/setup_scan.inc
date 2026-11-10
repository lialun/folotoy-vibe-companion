/* Phone-portal scan. Included here to exercise this exact handler with host
 * driver/HTTP stubs, without starting the production network task in a test. */
static esp_err_t setup_scan_error(httpd_req_t *r,const char *body) {
    httpd_resp_set_status(r,"503 Service Unavailable");
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_sendstr(r,body);
}
static esp_err_t setup_scan(httpd_req_t *r) {
    if (!local_setup_request(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Use device Wi-Fi");
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    bool connecting=setup_phase==SETUP_CONNECTING;
    xSemaphoreGive(v_mutex);
    if (connecting) return setup_scan_error(r,"{\"error\":\"scan_busy\"}");
    wifi_scan_config_t scan={.show_hidden=false};
    esp_err_t error=esp_wifi_scan_start(&scan,true);
    if (error!=ESP_OK) {
        ESP_LOGW(TAG,"Portal scan start failed (%s)",esp_err_to_name(error));
        return setup_scan_error(r,error==ESP_ERR_WIFI_STATE?"{\"error\":\"scan_busy\"}":"{\"error\":\"scan_failed\"}");
    }
    uint16_t count=0;
    error=esp_wifi_scan_get_ap_num(&count);
    cJSON *rows=error==ESP_OK?cJSON_CreateArray():NULL;
    if (error==ESP_OK && !rows) error=ESP_ERR_NO_MEM;
    /* Drain one record at a time instead of duplicating sixteen driver records
     * on the heap. The driver returns strongest first; keep the existing cap. */
    for (unsigned i=0;error==ESP_OK && i<count && i<16;i++) {
        wifi_ap_record_t record;
        error=esp_wifi_scan_get_ap_record(&record);
        if (error!=ESP_OK) break;
        record.ssid[sizeof(record.ssid)-1]=0;
        cJSON *row=cJSON_CreateObject();
        if (!row || !cJSON_AddStringToObject(row,"ssid",(char *)record.ssid) ||
            !cJSON_AddNumberToObject(row,"rssi",record.rssi) || !cJSON_AddItemToArray(rows,row)) {
            cJSON_Delete(row);error=ESP_ERR_NO_MEM;
        }
    }
    /* Including zero-result scans, truncation and every allocation/read error. */
    esp_wifi_clear_ap_list();
    if (error!=ESP_OK) {
        cJSON_Delete(rows);
        ESP_LOGW(TAG,"Portal scan results failed (%s)",esp_err_to_name(error));
        return setup_scan_error(r,error==ESP_ERR_NO_MEM?"{\"error\":\"scan_memory\"}":"{\"error\":\"scan_failed\"}");
    }
    return json_response(r,rows);
}
