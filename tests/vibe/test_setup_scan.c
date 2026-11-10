/* Exercise the production portal handler, including driver-owned allocations. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_err.h"
#define ESP_ERR_WIFI_STATE 0x3006
#define ESP_LOGW(...) ((void)0)
#define HTTPD_403_FORBIDDEN 403
#define portMAX_DELAY 0
#define SETUP_CONNECTING 1
typedef struct { char body[8192];int status;bool local; } httpd_req_t;
typedef struct { bool show_hidden; } wifi_scan_config_t;
typedef struct { uint8_t ssid[33];int8_t rssi; } wifi_ap_record_t;
static int v_mutex,setup_phase,locks;
static unsigned starts,reads,clears,available,owned,live,allocations,fail_at;
static int read_failure;
static esp_err_t start_error,count_error;
static void *allocate(size_t size) {
    if (++allocations==fail_at) return NULL;
    void *p=malloc(size);if (p) ++live;return p;
}
static void release(void *p) { if (p) { assert(live);--live;free(p); } }
static void xSemaphoreTake(int mutex,int wait) { (void)mutex;(void)wait;assert(!locks);++locks; }
static void xSemaphoreGive(int mutex) { (void)mutex;assert(locks==1);--locks; }
static bool local_setup_request(httpd_req_t *r) { return r->local; }
static esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *status) { r->status=atoi(status);return ESP_OK; }
static esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *type) { (void)r;assert(!strcmp(type,"application/json"));return ESP_OK; }
static esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value) {
    (void)r;assert(!strcmp(key,"Cache-Control") && !strcmp(value,"no-store"));return ESP_OK;
}
static esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *text) {
    assert(strlen(text)<sizeof(r->body));strcpy(r->body,text);return ESP_OK;
}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *text) { r->status=status;return httpd_resp_sendstr(r,text); }
static esp_err_t json_response(httpd_req_t *r,cJSON *object) {
    char *text=cJSON_PrintUnformatted(object);cJSON_Delete(object);
    if (!text) return httpd_resp_send_err(r,500,"No memory");
    esp_err_t result=httpd_resp_sendstr(r,text);cJSON_free(text);return result;
}
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config,bool block) {
    assert(!locks && block && !config->show_hidden && !owned);++starts;
    if (start_error) return start_error;
    owned=available;return ESP_OK;
}
static esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count) { *count=available;return count_error; }
static esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *record) {
    if ((int)reads==read_failure) return ESP_FAIL;
    assert(owned);--owned;memset(record,0,sizeof(*record));
    snprintf((char *)record->ssid,sizeof(record->ssid),"Station %u",reads);record->rssi=-30-(int)reads;++reads;return ESP_OK;
}
static esp_err_t esp_wifi_clear_ap_list(void) { owned=0;++clears;return ESP_OK; }
#include "../../main/vibe/setup_scan.inc"
static void reset(unsigned count) {
    assert(!live && !owned && !locks);
    setup_phase=0;starts=reads=clears=allocations=fail_at=0;read_failure=-1;
    available=count;start_error=count_error=ESP_OK;
}
static httpd_req_t request(void) { return (httpd_req_t){.status=200,.local=true}; }
static void check_error(httpd_req_t *r,const char *code) {
    assert(r->status==503);char expected[80];snprintf(expected,sizeof(expected),"{\"error\":\"%s\"}",code);
    assert(!strcmp(r->body,expected));assert(!owned && !live);
}
int main(void) {
    cJSON_Hooks hooks={allocate,release};cJSON_InitHooks(&hooks);
    reset(3);httpd_req_t r=request();r.local=false;setup_scan(&r);assert(r.status==403 && !starts);
    reset(3);setup_phase=SETUP_CONNECTING;r=request();setup_scan(&r);check_error(&r,"scan_busy");assert(!starts);
    reset(3);start_error=ESP_ERR_WIFI_STATE;r=request();setup_scan(&r);check_error(&r,"scan_busy");assert(starts==1 && !clears);
    reset(3);start_error=ESP_FAIL;r=request();setup_scan(&r);check_error(&r,"scan_failed");
    reset(3);count_error=ESP_FAIL;r=request();setup_scan(&r);check_error(&r,"scan_failed");assert(clears==1);
    reset(3);read_failure=1;r=request();setup_scan(&r);check_error(&r,"scan_failed");assert(reads==1 && clears==1);
    reset(0);r=request();setup_scan(&r);assert(r.status==200 && !strcmp(r.body,"[]") && clears==1 && !live);
    reset(3);r=request();setup_scan(&r);assert(r.status==200 && reads==3 && clears==1 && !owned && !live);
    cJSON *rows=cJSON_Parse(r.body);assert(cJSON_GetArraySize(rows)==3);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(rows,2),"ssid")->valuestring,"Station 2"));cJSON_Delete(rows);
    /* Every JSON allocation/serialization failure releases the partial response
     * and all unread driver records; it never reports a false empty success. */
    for (unsigned failure=1;failure<160;failure++) {
        reset(20);fail_at=failure;r=request();setup_scan(&r);
        assert((r.status==200 || r.status==500 || r.status==503) && clears==1 && !owned && !live);
        if (r.status==503) check_error(&r,"scan_memory");
        if (r.status==200) assert(reads==16 && strcmp(r.body,"[]"));
    }
    for (unsigned i=0;i<100;i++) {
        reset(40);r=request();setup_scan(&r);assert(r.status==200 && reads==16 && clears==1 && !owned && !live);
    }
    puts("Portal scan: busy/failure versus empty, bounded streaming records, every JSON failure and repeated cleanup PASS");
}
