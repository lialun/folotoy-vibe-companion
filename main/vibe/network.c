#include "vibe.h"
#include "radio/relay.h"
#include "diagnostics.h"
#include "portal.h"
#include "peer.h"
#include "transport.h"
#include "ble.h"
#include "wifi_profiles.h"
#include "wifi_retry.h"
#include "clock_sync.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_vfs_eventfd.h"
#include <unistd.h>
#include "lwip/sockets.h"
#include "nvs.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG="vibe_net";
static int udp=-1;
static int wake_fd=-1;
static int dns_socket=-1;
static QueueHandle_t net_commands;
static bool wifi_running=true;
static httpd_handle_t web;
static bool ap_started,ap_automatic,testing_credentials;
static uint64_t credentials_at,boot_at,setup_saved_at;
static vibe_wifi_credential_t candidate;
static char active_ssid[33];
static vibe_wifi_profiles_t networks;
static SemaphoreHandle_t profiles_mutex;
static vibe_wifi_retry_t auto_connection;
typedef enum { SETUP_SELECT,SETUP_CONNECTING,SETUP_CONNECTED,SETUP_FAILED } setup_phase_t;
static setup_phase_t setup_phase; /* Protected by v_mutex, including HTTP access. */
static bool profiles_persist(const void *blob,size_t size,void *context) {
    (void)context;nvs_handle_t n;
    if (nvs_open("vibe_wifi",NVS_READWRITE,&n)!=ESP_OK) return false;
    bool ok=nvs_set_blob(n,"config",blob,size)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);return ok;
}
static void profiles_load(void) {
    nvs_handle_t n;if (nvs_open("vibe_wifi",NVS_READONLY,&n)!=ESP_OK) return;
    uint8_t blob[VIBE_WIFI_BLOB_MAX];size_t size=sizeof(blob);
    bool loaded=nvs_get_blob(n,"config",blob,&size)==ESP_OK && vibe_wifi_profiles_decode(&networks,blob,size);
    memset(blob,0,sizeof(blob));
    if (!loaded) {
        /* Replace the legacy single-network value only after a validated write.
         * Using the same key avoids retaining forgotten legacy credentials. */
        wifi_config_t legacy={0};size=sizeof(legacy);
        if (nvs_get_blob(n,"config",&legacy,&size)==ESP_OK && size==sizeof(legacy)) {
            vibe_wifi_credential_t entry={0};
            memcpy(entry.ssid,legacy.sta.ssid,32);memcpy(entry.password,legacy.sta.password,63);
            vibe_wifi_profiles_t next={0};
            if (vibe_wifi_profiles_put(&next,&entry)) {
                nvs_close(n);n=0;
                if (!vibe_wifi_profiles_commit(&networks,&next,profiles_persist,NULL)) {
                    networks=next;ESP_LOGW(TAG,"Wi-Fi profile migration deferred; legacy network retained");
                }
            }
            memset(&entry,0,sizeof(entry));
        }
        memset(&legacy,0,sizeof(legacy));
    }
    if (n) nvs_close(n);
}
static bool connect_network(const vibe_wifi_credential_t *entry) {
    wifi_config_t config={0};
    memcpy(config.sta.ssid,entry->ssid,strlen(entry->ssid));
    memcpy(config.sta.password,entry->password,strlen(entry->password));
    // Publish the new target before disconnect queues events for the old SSID.
    xSemaphoreTake(profiles_mutex,portMAX_DELAY);strlcpy(active_ssid,entry->ssid,sizeof(active_ssid));xSemaphoreGive(profiles_mutex);
    esp_wifi_disconnect();
    bool ok=esp_wifi_set_config(WIFI_IF_STA,&config)==ESP_OK && esp_wifi_connect()==ESP_OK;
    memset(&config,0,sizeof(config));
    return ok;
}
static void remember_connected(void) {
    xSemaphoreTake(profiles_mutex,portMAX_DELAY);
    int found=vibe_wifi_profiles_find(&networks,active_ssid);
    if (found>=0 && networks.preferred!=(unsigned)found) {
        vibe_wifi_profiles_t next=networks;next.preferred=(uint8_t)found;
        if (!vibe_wifi_profiles_commit(&networks,&next,profiles_persist,NULL))
            ESP_LOGW(TAG,"Wi-Fi preference save deferred");
    }
    xSemaphoreGive(profiles_mutex);
}
void vibe_network_wake(void) {
    /* A UDP loopback wake needs an UP IP interface in lwIP. BLE-only mode has
     * none. eventfd wakes select directly, coalesces events and needs no polling
     * task, packet buffer, RF activity or additional periodic timer. */
    if (wake_fd>=0) { uint64_t one=1;(void)write(wake_fd,&one,sizeof(one)); }
}
int vibe_wifi_send(vibe_route_t route,unsigned char type,const void *bytes,size_t n) {
    if (udp<0 || route.kind!=V_LINK_WIFI || !route.address || !route.port || n>VIBE_JSON_MAX) return -1;
    struct sockaddr_in target={.sin_family=AF_INET,.sin_port=route.port,.sin_addr.s_addr=route.address};
    struct iovec parts[2]={{.iov_base=&type,.iov_len=1},{.iov_base=(void *)bytes,.iov_len=n}};
    struct msghdr message={.msg_name=&target,.msg_namelen=sizeof(target),.msg_iov=parts,.msg_iovlen=2};
    return sendmsg(udp,&message,MSG_DONTWAIT)==(ssize_t)(n+1)?0:-1;
}
static void ui_provision(bool show) {
    vibe_msg_t m={.type=V_MSG_PROVISION,.event=show}; xQueueSend(v_queue,&m,pdMS_TO_TICKS(100));
}
void vibe_provision_start(void) { int cmd=1; (void)xQueueSend(net_commands,&cmd,0); vibe_network_wake(); }
void vibe_provision_finish(void) { int cmd=2; (void)xQueueSend(net_commands,&cmd,0); vibe_network_wake(); }
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ip=data;
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        v_status.wifi=true;
        snprintf(v_status.ip,sizeof(v_status.ip),IPSTR,IP2STR(&ip->ip_info.ip));
        xSemaphoreGive(v_mutex);
        /* Radio follows its computer owner, not Wi-Fi association. A background
         * Wi-Fi reconnect must not interrupt healthy BLE listening. The owner
         * controller wakes audio when the selected route actually changes. */
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *reason=data;
        wifi_ap_record_t linked;
        bool associated=esp_wifi_sta_get_ap_info(&linked)==ESP_OK;
        xSemaphoreTake(profiles_mutex,portMAX_DELAY);
        bool relevant=vibe_wifi_disconnect_relevant(active_ssid,reason?reason->ssid:NULL,
            reason?reason->ssid_len:0,associated);
        xSemaphoreGive(profiles_mutex);
        if (!relevant) return;
        vibe_msg_t lost={.type=V_MSG_LINK_LOST,.event=1};(void)xQueueSend(v_queue,&lost,0);
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        v_status.wifi=false;v_status.signal=(vibe_signal_t){0};
        xSemaphoreGive(v_mutex);
        /* Intentional disconnects precede set_config/connect; do not mistake
         * their delayed callbacks for failure of the replacement attempt. */
        if (!reason || reason->reason!=WIFI_REASON_ASSOC_LEAVE) {
            ESP_LOGI(TAG,"Wi-Fi disconnected: reason=%u",reason?(unsigned)reason->reason:0);
            int command=4;(void)xQueueSend(net_commands,&command,0);vibe_network_wake();
        }
    }
}
static bool local_setup_request(httpd_req_t *r) {
    struct sockaddr_storage local; socklen_t n=sizeof(local);
    return getsockname(httpd_req_to_sockfd(r),(struct sockaddr *)&local,&n)==0 &&
        vibe_portal_local_address((const struct sockaddr *)&local,n);
}
static esp_err_t json_response(httpd_req_t *r,cJSON *o) {
    char *s=cJSON_PrintUnformatted(o); cJSON_Delete(o);
    if (!s) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_sendstr(r,s); free(s); return e;
}
static esp_err_t setup_page(httpd_req_t *r) {
    if (!local_setup_request(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Use device Wi-Fi");
    char host[80];
    if (httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK || !vibe_portal_host(host)) {
        httpd_resp_set_status(r,"303 See Other");
        httpd_resp_set_hdr(r,"Location",VIBE_PORTAL_URL);
        return httpd_resp_sendstr(r,"Open the device Wi-Fi setup page.");
    }
    extern const unsigned char provisioning_html_start[] asm("_binary_provisioning_html_start");
    extern const unsigned char provisioning_html_end[] asm("_binary_provisioning_html_end");
    httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; frame-ancestors 'none'");
    return httpd_resp_send(r,(const char *)provisioning_html_start,provisioning_html_end-provisioning_html_start);
}
static esp_err_t setup_status(httpd_req_t *r) {
    if (!local_setup_request(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Use device Wi-Fi");
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    vibe_status_t s=v_status; setup_phase_t phase=setup_phase;
    xSemaphoreGive(v_mutex);
    cJSON *o=cJSON_CreateObject();
    cJSON_AddBoolToObject(o,"connected",s.wifi && phase==SETUP_CONNECTED);
    cJSON_AddBoolToObject(o,"connecting",phase==SETUP_CONNECTING);
    cJSON_AddBoolToObject(o,"failed",phase==SETUP_FAILED);
    cJSON_AddStringToObject(o,"message",s.notice); return json_response(r,o);
}
static esp_err_t setup_saved(httpd_req_t *r) {
    if (!local_setup_request(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Use device Wi-Fi");
    xSemaphoreTake(v_mutex,portMAX_DELAY);bool connected=v_status.wifi;xSemaphoreGive(v_mutex);
    cJSON *o=cJSON_CreateObject(),*rows=cJSON_AddArrayToObject(o,"networks");
    cJSON_AddNumberToObject(o,"capacity",VIBE_WIFI_PROFILE_MAX);
    xSemaphoreTake(profiles_mutex,portMAX_DELAY);
    for (unsigned i=0;i<networks.count;i++) {
        cJSON *row=cJSON_CreateObject();cJSON_AddStringToObject(row,"ssid",networks.entries[i].ssid);
        cJSON_AddBoolToObject(row,"connected",connected && !strcmp(active_ssid,networks.entries[i].ssid));
        cJSON_AddBoolToObject(row,"preferred",i==networks.preferred);cJSON_AddItemToArray(rows,row);
    }
    xSemaphoreGive(profiles_mutex);
    return json_response(r,o);
}
#include "setup_scan.inc"
static bool setup_submission(httpd_req_t *r) {
    if (!local_setup_request(r)) return false;
    char value[80];
    if (httpd_req_get_hdr_value_str(r,"Host",value,sizeof(value))!=ESP_OK || !vibe_portal_host(value)) return false;
    if (httpd_req_get_hdr_value_str(r,"X-Vibe-Setup",value,sizeof(value))!=ESP_OK || strcmp(value,"1")) return false;
    /* Some captive WebViews omit Origin. The custom header still requires a
     * same-origin fetch; cross-origin preflight is never allowed. */
    return !httpd_req_get_hdr_value_len(r,"Origin") ||
        (httpd_req_get_hdr_value_str(r,"Origin",value,sizeof(value))==ESP_OK && vibe_portal_origin(value));
}
static esp_err_t setup_forget(httpd_req_t *r) {
    if (!setup_submission(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Open http://192.168.4.1");
    if (r->content_len<2 || r->content_len>160) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid request");
    char body[161];int got=0;
    while (got<r->content_len) {
        int n=httpd_req_recv(r,body+got,r->content_len-got);if (n<=0) return ESP_FAIL;got+=n;
    }
    body[got]=0;
    if (!vibe_json_depth_ok(body,got)) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid JSON");
    cJSON *o=cJSON_Parse(body);const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(o,"ssid");
    xSemaphoreTake(v_mutex,portMAX_DELAY);bool busy=setup_phase==SETUP_CONNECTING;xSemaphoreGive(v_mutex);
    bool ok=false;
    if (!busy && cJSON_IsString(ssid) && strlen(ssid->valuestring)<=32) {
        xSemaphoreTake(profiles_mutex,portMAX_DELAY);vibe_wifi_profiles_t next=networks;
        ok=vibe_wifi_profiles_remove(&next,ssid->valuestring) && vibe_wifi_profiles_commit(&networks,&next,profiles_persist,NULL);
        memset(&next,0,sizeof(next));xSemaphoreGive(profiles_mutex);
    }
    cJSON_Delete(o);
    if (!ok) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Unable to forget network");
    httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");
}
static esp_err_t setup_save(httpd_req_t *r) {
    if (!setup_submission(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Open http://192.168.4.1");
    if (r->content_len<2 || r->content_len>256)
        return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid request or connection in progress");
    char body[257]; int got=0;
    while (got<r->content_len) {
        int n=httpd_req_recv(r,body+got,r->content_len-got);
        if (n<=0) return ESP_FAIL;
        got+=n;
    }
    body[got]=0;
    if (!vibe_json_depth_ok(body,got)) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid JSON");
    cJSON *o=cJSON_Parse(body);
    cJSON *ssid=cJSON_GetObjectItemCaseSensitive(o,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(o,"password");
    bool use_saved=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"useSaved"));
    vibe_wifi_credential_t entry={0};
    bool valid=cJSON_IsString(ssid) && strlen(ssid->valuestring)>0 && strlen(ssid->valuestring)<=32;
    if (valid) {
        strlcpy(entry.ssid,ssid->valuestring,sizeof(entry.ssid));
        xSemaphoreTake(profiles_mutex,portMAX_DELAY);int found=vibe_wifi_profiles_find(&networks,entry.ssid);
        if (use_saved) { valid=found>=0;if (valid) entry=networks.entries[found]; }
        else {
            valid=(found>=0 || networks.count<VIBE_WIFI_PROFILE_MAX) && cJSON_IsString(password) && strlen(password->valuestring)<=63;
            if (valid) strlcpy(entry.password,password->valuestring,sizeof(entry.password));
        }
        xSemaphoreGive(profiles_mutex);
    }
    valid=valid && vibe_wifi_credential_valid(&entry);
    if (!valid) { cJSON_Delete(o); return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"SSID/password length invalid"); }
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    if (setup_phase==SETUP_CONNECTING) {
        xSemaphoreGive(v_mutex); cJSON_Delete(o);
        return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Connection in progress");
    }
    candidate=entry;memset(&entry,0,sizeof(entry));
    setup_phase=SETUP_CONNECTING;
    strlcpy(v_status.notice,"正在连接，请稍等",sizeof(v_status.notice));
    xSemaphoreGive(v_mutex);
    cJSON_Delete(o);
    int cmd=3;
    if (xQueueSend(net_commands,&cmd,pdMS_TO_TICKS(100))!=pdTRUE) {
        xSemaphoreTake(v_mutex,portMAX_DELAY); setup_phase=SETUP_FAILED; xSemaphoreGive(v_mutex);
        return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Busy");
    }
    httpd_resp_set_type(r,"application/json"); return httpd_resp_sendstr(r,"{\"ok\":true}");
}
static esp_err_t portal_redirect(httpd_req_t *r,httpd_err_code_t error) {
    (void)error;
    if (!local_setup_request(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Use device Wi-Fi");
    httpd_resp_set_status(r,"303 See Other");
    httpd_resp_set_hdr(r,"Location",VIBE_PORTAL_URL);
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    /* iOS captive detection needs a nonempty response, not just Location. */
    return httpd_resp_sendstr(r,"Open the device Wi-Fi setup page.");
}
static void portal_dns_start(void) {
    esp_netif_t *ap=esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap) return;
    esp_netif_dns_info_t dns={.ip={.type=ESP_IPADDR_TYPE_V4,.u_addr.ip4.addr=inet_addr(VIBE_PORTAL_IP)}};
    uint8_t offer_dns=2; /* DHCP server OFFER_DNS flag, as in the IDF reference. */
    static char portal_url[]=VIBE_PORTAL_URL;
    esp_netif_dhcps_stop(ap);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(ap,ESP_NETIF_DNS_MAIN,&dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer_dns,sizeof(offer_dns)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_CAPTIVEPORTAL_URI,portal_url,strlen(portal_url)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(ap));
    dns_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(53),.sin_addr.s_addr=inet_addr(VIBE_PORTAL_IP)};
    if (dns_socket>=0 && bind(dns_socket,(struct sockaddr *)&address,sizeof(address))<0) {
        close(dns_socket); dns_socket=-1;
    }
    if (dns_socket<0) ESP_LOGW(TAG,"Captive DNS unavailable; manual setup URL remains usable");
}
static void portal_dns_poll(void) {
    if (dns_socket<0) return;
    static uint8_t query[513],reply[512];
    struct sockaddr_in from; socklen_t from_len=sizeof(from);
    int n=recvfrom(dns_socket,query,sizeof(query),MSG_DONTWAIT,(struct sockaddr *)&from,&from_len);
    if (n<=0) return;
    size_t count=vibe_portal_dns_reply(query,n,reply,sizeof(reply));
    if (count) sendto(dns_socket,reply,count,MSG_DONTWAIT,(struct sockaddr *)&from,from_len);
}
static void start_ap(bool automatic) {
    ap_automatic=automatic;
    if (ap_started) { ui_provision(true); return; }
    setup_saved_at=0;
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    snprintf(v_status.ap_name,sizeof(v_status.ap_name),"Vibe-Setup-%.6s",v_status.device_id+6);
    setup_phase=SETUP_SELECT;
    strlcpy(v_status.notice,"选择 Wi-Fi",sizeof(v_status.notice));
    wifi_config_t ap={0};
    strlcpy((char *)ap.ap.ssid,v_status.ap_name,sizeof(ap.ap.ssid));
    ap.ap.ssid_len=strlen(v_status.ap_name); ap.ap.channel=1; ap.ap.max_connection=2;
    ap.ap.authmode=WIFI_AUTH_OPEN; v_status.provisioning=true;
    bool connected=v_status.wifi;
    xSemaphoreGive(v_mutex);
    /* A saved-but-unavailable network must not keep the driver connecting
     * while the phone scans. Portal mode owns disconnected STA attempts until
     * a submitted network is tested or the portal is closed. */
    auto_connection.pending=false;
    if (!connected) esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_APSTA); esp_wifi_set_config(WIFI_IF_AP,&ap);
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.stack_size=6144; config.max_uri_handlers=6; config.max_open_sockets=4; config.lru_purge_enable=true;
    config.recv_wait_timeout=3; config.send_wait_timeout=3;
    if (httpd_start(&web,&config)==ESP_OK) {
        const httpd_uri_t routes[]={
            {.uri="/",.method=HTTP_GET,.handler=setup_page},
            {.uri="/status",.method=HTTP_GET,.handler=setup_status},
            {.uri="/scan",.method=HTTP_GET,.handler=setup_scan},
            {.uri="/saved",.method=HTTP_GET,.handler=setup_saved},
            {.uri="/forget",.method=HTTP_POST,.handler=setup_forget},
            {.uri="/save",.method=HTTP_POST,.handler=setup_save}};
        for (size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) httpd_register_uri_handler(web,&routes[i]);
        httpd_register_err_handler(web,HTTPD_404_NOT_FOUND,portal_redirect);
    }
    portal_dns_start();
    ap_started=true; ui_provision(true);
}
static void finish_ap(void) {
    setup_saved_at=0;
    if (dns_socket>=0) { close(dns_socket); dns_socket=-1; }
    if (web) { httpd_stop(web); web=NULL; }
    esp_wifi_set_mode(WIFI_MODE_STA); ap_started=false;
    vibe_wifi_retry_reset(&auto_connection,vibe_now());
    xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.provisioning=false; xSemaphoreGive(v_mutex);
    ui_provision(false);
}
static void network_task(void *arg) {
    (void)arg;
    udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_IP);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(VIBE_PORT),.sin_addr.s_addr=INADDR_ANY};
    struct timeval timeout={.tv_sec=0,.tv_usec=100000};
    if (udp<0 || bind(udp,(struct sockaddr *)&address,sizeof(address))<0) {
        ESP_LOGE(TAG,"Cannot bind voice port");if (udp>=0) close(udp);udp=-1;vTaskDelete(NULL);return;
    }
    uint64_t signal_at=0;
    bool ever_connected=false,provisioning_offered=false,connected_handled=false;
    vibe_clock_sync_t clock_sync={0};
    vibe_wifi_retry_reset(&auto_connection,vibe_now());
    for (;;) {
        uint64_t now=vibe_now(); int cmd;
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        bool clock_wifi=v_status.wifi,clock_paused=v_model.recording || v_status.provisioning;
        xSemaphoreGive(v_mutex);
        vibe_clock_poll(&clock_sync,now,clock_wifi,clock_paused);
        vibe_weather_poll(now);
        while (xQueueReceive(net_commands,&cmd,0)==pdTRUE) {
            if (cmd==1) { provisioning_offered=true;if (!wifi_running) { esp_wifi_start();wifi_running=true; xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.wifi_enabled=true;xSemaphoreGive(v_mutex); }start_ap(false); }
            else if (cmd==2) {
                if (testing_credentials) { testing_credentials=false;memset(&candidate,0,sizeof(candidate));vibe_wifi_retry_reset(&auto_connection,now); }
                finish_ap();
            }
            else if (cmd==3) {
                setup_saved_at=0;
                xSemaphoreTake(v_mutex,portMAX_DELAY);
                v_status.wifi=false; strlcpy(v_status.notice,"正在连接 Wi-Fi",sizeof(v_status.notice));
                xSemaphoreGive(v_mutex);
                testing_credentials=true; credentials_at=now;
                auto_connection.pending=false;(void)connect_network(&candidate);
            } else if (cmd==4 && !testing_credentials && !ap_started) {
                // The callback and network worker are asynchronous. Recheck the
                // driver before applying an older failure to a replacement link.
                wifi_ap_record_t linked;
                if (esp_wifi_sta_get_ap_info(&linked)==ESP_OK) continue;
                xSemaphoreTake(profiles_mutex,portMAX_DELAY);unsigned count=networks.count;xSemaphoreGive(profiles_mutex);
                if (auto_connection.pending) vibe_wifi_retry_failed(&auto_connection,now,count);
                else { vibe_wifi_retry_reset(&auto_connection,now);auto_connection.due=now+1000; }
            }
        }
        vibe_status_t s; vibe_model_t mode;vibe_snapshot(&mode,NULL,&s);
        /* Acknowledge wireless changes over the old route before stopping it. */
        vibe_diagnostics_flush();vibe_transport_flush();
        if (wifi_running!=s.wifi_enabled) {
            wifi_running=s.wifi_enabled;
            if (wifi_running) { esp_wifi_start();esp_wifi_set_ps(WIFI_PS_MIN_MODEM);vibe_wifi_retry_reset(&auto_connection,now); }
            else { if (ap_started) finish_ap();esp_wifi_stop();vibe_msg_t lost={.type=V_MSG_LINK_LOST,.event=1};(void)xQueueSend(v_queue,&lost,0); }
        }
        (void)vibe_ble_enable(s.ble_enabled);
        vibe_ble_poll(now,(mode.recording || (mode.radio_active && mode.radio_requested)) && s.link==V_LINK_BLE,s.ble_peer);
        if (ap_started && ap_automatic && s.peer && s.link==V_LINK_BLE) finish_ap();
        if (!s.wifi) connected_handled=false;
        if (testing_credentials) {
            if (s.wifi) {
                xSemaphoreTake(profiles_mutex,portMAX_DELAY);vibe_wifi_profiles_t next=networks;
                bool stored=vibe_wifi_profiles_put(&next,&candidate) && vibe_wifi_profiles_commit(&networks,&next,profiles_persist,NULL);
                memset(&next,0,sizeof(next));xSemaphoreGive(profiles_mutex);
                if (stored) setup_saved_at=now;
                memset(&candidate,0,sizeof(candidate)); testing_credentials=false;
                xSemaphoreTake(v_mutex,portMAX_DELAY);
                setup_phase=stored?SETUP_CONNECTED:SETUP_FAILED;
                strlcpy(v_status.notice,stored?"连接成功，正在返回首页":"保存失败，请重新连接",sizeof(v_status.notice));
                xSemaphoreGive(v_mutex);
            } else if (now-credentials_at>20000) {
                testing_credentials=false; memset(&candidate,0,sizeof(candidate));
                esp_wifi_disconnect();vibe_wifi_retry_reset(&auto_connection,now);auto_connection.due=now+5000;
                xSemaphoreTake(v_mutex,portMAX_DELAY); setup_phase=SETUP_FAILED;
                strlcpy(v_status.notice,"连接失败，请检查密码和 2.4GHz Wi-Fi",sizeof(v_status.notice)); xSemaphoreGive(v_mutex);
            }
        } else if (s.wifi) {
            auto_connection.pending=false;
            if (!connected_handled) { remember_connected();connected_handled=true;vibe_wifi_retry_reset(&auto_connection,now); }
            if (ap_started && ap_automatic && !setup_saved_at) {
                setup_saved_at=now;
                xSemaphoreTake(v_mutex,portMAX_DELAY);setup_phase=SETUP_CONNECTED;
                strlcpy(v_status.notice,"已连接保存的 Wi-Fi",sizeof(v_status.notice));xSemaphoreGive(v_mutex);
            }
        } else if (!ap_started && s.wifi_enabled) {
            xSemaphoreTake(profiles_mutex,portMAX_DELAY);unsigned count=networks.count;
            xSemaphoreGive(profiles_mutex);
            if (vibe_wifi_retry_timeout(&auto_connection,now)) { vibe_wifi_retry_failed(&auto_connection,now,count);esp_wifi_disconnect(); }
            if (vibe_wifi_retry_due(&auto_connection,now,count)) {
                // Timeout may have advanced the attempt: select after that step.
                xSemaphoreTake(profiles_mutex,portMAX_DELAY);
                unsigned index=vibe_wifi_retry_index(networks.count,networks.preferred,auto_connection.attempt);
                vibe_wifi_credential_t entry=networks.entries[index];
                xSemaphoreGive(profiles_mutex);
                vibe_wifi_retry_started(&auto_connection,now);
                ESP_LOGI(TAG,"Auto Wi-Fi attempt=%u/%u",index+1,count);
                if (!connect_network(&entry)) vibe_wifi_retry_failed(&auto_connection,now,count);
                memset(&entry,0,sizeof(entry));
            }
        }
        if (ap_started && setup_saved_at) {
            if (!s.wifi) {
                setup_saved_at=0;
                xSemaphoreTake(v_mutex,portMAX_DELAY); setup_phase=SETUP_FAILED;
                strlcpy(v_status.notice,"连接已中断，请重试",sizeof(v_status.notice)); xSemaphoreGive(v_mutex);
            } else if (vibe_portal_close_due(s.wifi,setup_saved_at,now)) finish_ap();
        }
        if (s.wifi) ever_connected=true;
        /* Only initial setup opens the hotspot automatically. A later outage
         * must not replace Home or interrupt the background timer. */
        xSemaphoreTake(profiles_mutex,portMAX_DELAY);bool has_saved=networks.count>0;xSemaphoreGive(profiles_mutex);
        wifi_ap_record_t linked;
        bool associated=!s.wifi && esp_wifi_sta_get_ap_info(&linked)==ESP_OK;
        if (s.wifi_enabled && !s.paired_count && !s.peer && !s.wifi && !ap_started && !ever_connected && !provisioning_offered &&
            vibe_wifi_offer_setup(&auto_connection,now,boot_at,has_saved,associated)) {
            provisioning_offered=true;start_ap(true);
        }
        portal_dns_poll();
        static unsigned char packet[VIBE_JSON_MAX+2]; struct sockaddr_in from; socklen_t len=sizeof(from);
        vibe_model_t power;vibe_snapshot(&power,NULL,NULL);
        unsigned wait_ms=ap_started?100:power.recording?50:power.screen_on?200:
            ((s.wifi_enabled && !s.wifi) || testing_credentials || auto_connection.pending)?1000:30000;
        wait_ms=vibe_clock_wait(&clock_sync,vibe_now(),wait_ms);
        timeout.tv_usec=(wait_ms%1000)*1000;timeout.tv_sec=wait_ms/1000;
        fd_set readable;FD_ZERO(&readable);FD_SET(udp,&readable);FD_SET(wake_fd,&readable);
        int ready=select((udp>wake_fd?udp:wake_fd)+1,&readable,NULL,NULL,&timeout);
        if (ready>0 && FD_ISSET(wake_fd,&readable)) { uint64_t pending;(void)read(wake_fd,&pending,sizeof(pending)); }
        int n=ready>0 && FD_ISSET(udp,&readable)?recvfrom(udp,packet,sizeof(packet)-1,MSG_DONTWAIT,(struct sockaddr *)&from,&len):0;
        now=vibe_now(); /* A long blocking receive must not age fresh beacons. */
        if (n>1 && packet[0]==5) {
            (void)vibe_relay_receive((vibe_route_t){.kind=V_LINK_WIFI,.address=from.sin_addr.s_addr,.port=from.sin_port},packet,(size_t)n);
        }
        if (n>1 && n<=VIBE_JSON_MAX+1 && (packet[0]==2 || packet[0]==3) && vibe_json_depth_ok((char *)packet+1,n-1)) {
            packet[n]=0;
            (void)vibe_message_post_from(v_queue,(char *)packet+1,n-1,
                (vibe_route_t){.kind=V_LINK_WIFI,.address=from.sin_addr.s_addr,.port=from.sin_port});
        }
        vibe_diagnostics_flush();vibe_transport_flush();
        if (s.wifi && power.screen_on && (!s.signal.valid || now-signal_at>=5000)) {
            signal_at=now;
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap)==ESP_OK) {
                xSemaphoreTake(v_mutex,portMAX_DELAY);
                if (v_status.wifi) {
                    bool first=!v_status.signal.valid;unsigned previous=v_status.signal.bars;
                    v_status.rssi=ap.rssi;vibe_signal_update(&v_status.signal,ap.rssi);
                    if (first || previous!=v_status.signal.bars)
                        ESP_LOGI(TAG,"Signal: RSSI %d dBm, filtered %d dBm, bars %u",ap.rssi,v_status.signal.filtered/256,v_status.signal.bars);
                }
                xSemaphoreGive(v_mutex);
            }
        }
    }
}
esp_err_t vibe_network_init(void) {
    esp_vfs_eventfd_config_t events={.max_fds=1};
    ESP_RETURN_ON_ERROR(esp_vfs_eventfd_register(&events),TAG,"Network wake registration failed");
    wake_fd=eventfd(0,0);if (wake_fd<0) return ESP_ERR_NO_MEM;
    profiles_mutex=xSemaphoreCreateMutex();net_commands=xQueueCreate(4,sizeof(int));
    if (!profiles_mutex || !net_commands) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta(); esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);
    profiles_load();
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_running=v_status.wifi_enabled;
    if (wifi_running) { ESP_ERROR_CHECK(esp_wifi_start());esp_wifi_set_ps(WIFI_PS_MIN_MODEM); }
    boot_at=vibe_now();
    if (xTaskCreate(network_task,"vibe_net",8192,NULL,4,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
