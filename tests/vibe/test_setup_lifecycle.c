/* Exercise the production portal lifetime without starting networking threads. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portMAX_DELAY 0
#define SETUP_SELECT 0
#define WIFI_MODE_APSTA 3
#define WIFI_MODE_STA 1
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_404_NOT_FOUND 404
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct {struct {uint8_t ssid[32];unsigned ssid_len,channel,max_connection,authmode;} ap;} wifi_config_t;
typedef struct {unsigned stack_size,max_uri_handlers,max_open_sockets,recv_wait_timeout,send_wait_timeout;bool lru_purge_enable;} httpd_config_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(void *);} httpd_uri_t;
static bool ap_started,wifi_started;
static unsigned step,fail_at,starts,stops,routes,dns_starts,dns_stops,wifi_stops,closed,opened,mode,locks,pauses,resumes;
static bool ble_live=true,restore_failure;
static uint64_t setup_saved_at;
static int v_mutex=1,profiles_mutex=2,setup_phase,dns_socket=-1;
static void *web;
typedef struct {char ap_name[33],device_id[18],notice[96];bool wifi,provisioning;} status_t;
static status_t v_status;
static struct {unsigned count;} networks;
static struct {bool pending;} auto_connection;
static bool fail(void) {return ++step==fail_at;}
static size_t copy_text(char *out,const char *in,size_t cap) {size_t n=strlen(in);snprintf(out,cap,"%s",in);return n;}
#define strlcpy copy_text
static void xSemaphoreTake(int m,int t) {(void)m;(void)t;assert(!locks);++locks;}
static void xSemaphoreGive(int m) {(void)m;assert(locks==1);--locks;}
static void ui_provision(int n) {assert(!locks);if(n==1)++opened;else {assert(n==0||n==2);++closed;}}
static esp_err_t setup_pause_ble(void) {
    assert(!locks && ble_live);++pauses;if(fail())return ESP_FAIL;ble_live=false;return ESP_OK;
}
static esp_err_t vibe_ble_init(void) {
    assert(!locks && !web && dns_socket==-1 && !ap_started && mode==WIFI_MODE_STA);++resumes;
    if(restore_failure)return ESP_ERR_NO_MEM;
    ble_live=true;return ESP_OK;
}
static esp_err_t esp_wifi_disconnect(void) {assert(!locks);return ESP_OK;}
static esp_err_t esp_wifi_set_mode(unsigned next) {assert(!locks);mode=next;return next==WIFI_MODE_APSTA&&fail()?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_set_config(unsigned iface,const wifi_config_t *config) {
    assert(!locks && iface==WIFI_IF_AP && config->ap.max_connection==2 && config->ap.authmode==WIFI_AUTH_OPEN);
    assert(!strcmp((const char *)config->ap.ssid,"Vibe-Setup-2f6bac"));return fail()?ESP_FAIL:ESP_OK;
}
static esp_err_t esp_wifi_start(void) {assert(!locks);return fail()?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_stop(void) {assert(!locks);++wifi_stops;return ESP_OK;}
static esp_err_t httpd_start(void **handle,const httpd_config_t *c) {
    assert(!locks && !web && c->stack_size==4096 && c->max_open_sockets==2 && c->lru_purge_enable);
    assert(c->max_uri_handlers==6 && c->recv_wait_timeout==3 && c->send_wait_timeout==3);
    ++starts;routes=0;if(fail())return ESP_ERR_NO_MEM;*handle=(void *)1;return ESP_OK;
}
static esp_err_t httpd_stop(void *handle) {assert(!locks && handle==web);++stops;return ESP_OK;}
static esp_err_t handler(void *r) {(void)r;return ESP_OK;}
#define setup_page handler
#define setup_status handler
#define setup_scan handler
#define setup_saved handler
#define setup_forget handler
#define setup_save handler
static esp_err_t portal_redirect(void *r,int error) {(void)r;(void)error;return ESP_OK;}
static esp_err_t httpd_register_uri_handler(void *handle,const httpd_uri_t *route) {
    assert(!locks && handle==web && route->uri && route->handler==handler);++routes;return fail()?ESP_ERR_NO_MEM:ESP_OK;
}
static esp_err_t httpd_register_err_handler(void *handle,int error,esp_err_t (*fn)(void *,int)) {
    assert(handle==web && error==404 && fn==portal_redirect);return fail()?ESP_FAIL:ESP_OK;
}
static void portal_dns_start(void) {assert(!locks && web && routes==6);++dns_starts;dns_socket=123;}
static int close(int fd) {assert(fd==123);++dns_stops;return 0;}
static uint64_t vibe_now(void) {return 1;}
static void vibe_wifi_retry_reset(void *retry,uint64_t now) {(void)now;assert(retry==&auto_connection);auto_connection.pending=false;}
#include "../../main/vibe/setup_lifecycle.inc"
static void reset(bool saved) {
    assert(!locks && !web && !ap_started && dns_socket==-1);
    step=fail_at=starts=stops=routes=dns_starts=dns_stops=wifi_stops=closed=opened=pauses=resumes=0;
    assert(!setup_ble_paused);ble_live=true;restore_failure=false;
    v_status=(status_t){.device_id="4c11ae2f6bac",.wifi=saved};
    wifi_started=false;networks.count=saved?1:0;auto_connection.pending=true;setup_saved_at=10;
}
int main(void) {
    for(unsigned saved=0;saved<2;saved++)for(unsigned failure=1;failure<=12;failure++) {
        reset(saved);fail_at=failure;start_ap();
        assert(!ap_started && !v_status.provisioning && !web && dns_socket==-1 && !opened && closed==1);
        assert(!dns_starts && !dns_stops && mode==WIFI_MODE_STA && !auto_connection.pending && !setup_saved_at);
        assert(stops==(failure>5?1u:0u) && wifi_stops==(saved?0u:1u));
        assert(ble_live && !setup_ble_paused && pauses==1 && resumes==1);
        fail_at=0;start_ap();assert(ap_started && web && opened==1);finish_ap(false);
        assert(!web && !ap_started && !v_status.provisioning);
    }
    for(unsigned cycle=0;cycle<100;cycle++) {
        reset(cycle%2);start_ap();assert(opened==1 && dns_starts==1 && ap_started && v_status.provisioning);
        start_ap();assert(opened==2 && starts==1 && dns_starts==1 && routes==6);
        finish_ap(true);finish_ap(false);assert(closed==1 && stops==1 && dns_stops==1 && !ap_started && !v_status.provisioning);
        assert(wifi_stops==(cycle%2?0u:1u));
    }
    reset(true);start_ap();restore_failure=true;finish_ap(false);
    assert(!ap_started && !web && setup_ble_paused && v_status.provisioning && !ble_live && ble_resume_at==2001);
    restore_failure=false;setup_restore_ble(2001);
    assert(ble_live && !setup_ble_paused && !v_status.provisioning && !ble_resume_at);
    puts("Portal lifecycle: exclusive Bluetooth/AP resources, 12 startup faults, deferred restore, cleanup, retained networks and 100 cycles PASS");
}
