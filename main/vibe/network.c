#include "vibe.h"
#include "portal.h"
#include "peer.h"
#include "ble.h"
#include "ble_ingress.h"
#include "voice_uplink.h"
#include "voice_power.h"
#include "wifi_power.h"
#include "radio/remote.h"
#include "radio/stream.h"
#include "wifi_profiles.h"
#include "wifi_retry.h"
#include "clock_sync.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "media_workspace.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG="vibe_net";
static int udp=-1;
/* Immutable after network initialization; only short loopback notifications use
 * this socket. Never write the worker's business socket from another task. */
static int wake_udp=-1;
static SemaphoreHandle_t wake_mutex;
static int dns_socket=-1;
static struct sockaddr_in peer;
static bool prefer_ble;
static bool ble_owner; /* A second authenticated link, independent of LAN. */
static struct sockaddr_in ble_peer,commit_peer;
static bool commit_ble;
static uint64_t wifi_seen,ble_seen;
static uint64_t owner_seen;
static uint32_t owner_generation=1,release_generation;
static char owner_client[37];
static bool release_after_send;
static uint64_t last_sent_ms; /* Protected by peer_mutex. */
static SemaphoreHandle_t peer_mutex;
static QueueHandle_t tx_queue,net_commands,ble_rx_queue;
static httpd_handle_t web;
static bool ap_started,wifi_started,testing_credentials;
static bool voice_streaming; /* Network worker only, including the repair tail. */
static atomic_bool radio_streaming; /* Audio worker intent; no UI mutex on TX. */
static vibe_wifi_power_t wifi_power;
static uint64_t credentials_at,setup_saved_at;
static vibe_wifi_credential_t candidate;
static char active_ssid[33];
static vibe_wifi_profiles_t networks;
static SemaphoreHandle_t profiles_mutex;
static vibe_wifi_retry_t auto_connection;
typedef enum { SETUP_SELECT,SETUP_CONNECTING,SETUP_CONNECTED,SETUP_FAILED } setup_phase_t;
static setup_phase_t setup_phase; /* Protected by v_mutex, including HTTP access. */
static unsigned event_sequence;
static uint32_t boot_nonce;
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
    /* The default fast scan can stop at a weak AP before seeing a stronger
     * AP with the same SSID. Re-evaluate all APs on a connection attempt. */
    config.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    /* A single association timeout must not immediately select a much weaker
     * AP with the same SSID. Keep retries bounded before trying the next AP. */
    config.sta.failure_retry_cnt=2;
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
    if (wake_udp>=0 && wake_mutex && xSemaphoreTake(wake_mutex,0)==pdTRUE) {
        struct sockaddr_in wake={.sin_family=AF_INET,.sin_port=htons(VIBE_PORT),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
        const char byte=0;sendto(wake_udp,&byte,1,MSG_DONTWAIT,(struct sockaddr *)&wake,sizeof(wake));
        xSemaphoreGive(wake_mutex);
    }
}
void vibe_network_release(void) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);release_generation=owner_generation;release_after_send=true;xSemaphoreGive(peer_mutex);
    vibe_network_wake();
}
uint32_t vibe_network_generation(void) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);uint32_t generation=owner_generation;xSemaphoreGive(peer_mutex);return generation;
}
bool vibe_network_generation_matches(uint32_t generation) {
    if (!generation) return true;
    xSemaphoreTake(peer_mutex,portMAX_DELAY);bool same=vibe_peer_generation_matches(generation,owner_generation);xSemaphoreGive(peer_mutex);return same;
}
bool vibe_connection_approved(const char *token,const char *client,const char *request) {
    vibe_connection_request_t *r=&v_status.connection_request;
    vibe_connect_tick(r,vibe_now());
    if (r->phase!=V_CONNECT_ACCEPTED || !vibe_connect_digest(token)) return false;
    unsigned char hash[32];char hex[65];
    if (mbedtls_sha256((const unsigned char *)token,64,hash,0)) return false;
    for (unsigned i=0;i<32;i++) snprintf(hex+i*2,3,"%02x",hash[i]);
    return vibe_connect_matches(r,client,request,hex);
}
#include "link_owner.inc"
void vibe_network_commit(const char *client) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    select_owner_locked(client,v_status.token);
    ble_owner=commit_ble;
    if (commit_ble) { ble_peer=commit_peer;ble_seen=vibe_now(); }
    else { peer=commit_peer;wifi_seen=vibe_now(); }
    xSemaphoreGive(peer_mutex);
    v_status.peer_seen_ms=vibe_now();v_status.ble_peer=commit_ble;
}
void vibe_send_json(const char *s) {
    vibe_send_json_owned(s,0,V_DELIVERY_PRIMARY);
}
void vibe_send_json_both(const char *s) {
    vibe_send_json_owned(s,vibe_network_generation(),V_DELIVERY_BOTH);
}
void vibe_send_json_owned(const char *s,uint32_t owner,vibe_delivery_t delivery) {
    if (vibe_outbound_post(tx_queue,s,owner,delivery)) vibe_network_wake();
}
static int send_packet(unsigned char type,const void *bytes,size_t n,uint32_t expected_owner) {
    if (udp<0 || n>VIBE_JSON_MAX) return -1;
    static unsigned char packet[VIBE_JSON_MAX+1];
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    if (expected_owner && (expected_owner!=owner_generation || release_after_send)) {
        xSemaphoreGive(peer_mutex);return -1;
    }
    uint64_t now=vibe_now();uint32_t generation=owner_generation;
    bool use_wifi=vibe_peer_use_wifi(now,wifi_seen,ble_seen,peer.sin_addr.s_addr && peer.sin_port,ble_owner,prefer_ble),has_ble=ble_owner;
    struct sockaddr_in destination=peer,bluetooth=ble_peer;
    xSemaphoreGive(peer_mutex);
    /* NimBLE/lwIP calls must not run under an application ownership lock. */
    int result=-1;
    if (!use_wifi && has_ble) result=vibe_ble_send_to(ntohs(bluetooth.sin_port)-1,type,bytes,n);
    else if (destination.sin_addr.s_addr) {
        packet[0]=type;memcpy(packet+1,bytes,n);
        result=sendto(udp,packet,n+1,MSG_DONTWAIT,(struct sockaddr *)&destination,sizeof(destination))==(int)n+1?0:-1;
    }
    if (!result) {
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        if (generation==owner_generation) last_sent_ms=vibe_now();
        xSemaphoreGive(peer_mutex);
    }
    if (type==2) {
        static unsigned debug_tx;
        if (debug_tx++<10) ESP_LOGI("vibe_ble","Business TX wifi=%d ble=%d result=%d bytes=%u",use_wifi,has_ble,result,(unsigned)n);
    }
    return result;
}
unsigned vibe_voice_links(uint32_t owner) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=owner==owner_generation && !release_after_send;
    bool wifi=current && vibe_peer_use_wifi(vibe_now(),wifi_seen,0,
        peer.sin_addr.s_addr && peer.sin_port,false,false);
    unsigned links=(wifi?V_VOICE_WIFI:0u)|(current && ble_owner?V_VOICE_BLE:0u);
    xSemaphoreGive(peer_mutex);
    return links;
}

static unsigned voice_transport_links(uint32_t owner,vibe_voice_mode_t mode) {
    if (mode!=V_VOICE_LEGACY) return vibe_voice_links(owner);
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=owner==owner_generation && !release_after_send;
    bool wifi=current && vibe_peer_use_wifi(vibe_now(),wifi_seen,ble_seen,
        peer.sin_addr.s_addr && peer.sin_port,ble_owner,prefer_ble);
    unsigned links=wifi?V_VOICE_WIFI:current && ble_owner?V_VOICE_BLE:0u;
    xSemaphoreGive(peer_mutex);
    return links;
}

/* The network worker is the sole writer for V2 voice. Capture cannot enter
 * either stack or compete with this worker for BLE credits. */
static vibe_voice_submission_t voice_transport_send(vibe_voice_mode_t mode,const vibe_voice_tx_ticket_t *ticket,
                                const uint8_t *bytes,size_t length,int *network_error) {
    unsigned leg=ticket->leg,type=mode==V_VOICE_LEGACY?1:VIBE_VOICE_TYPE;
    uint32_t owner=ticket->owner;
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=owner==owner_generation && !release_after_send;
    struct sockaddr_in destination=leg==0?peer:ble_peer;
    bool available=current && (leg==0 ?
        vibe_peer_use_wifi(vibe_now(),wifi_seen,0,peer.sin_addr.s_addr && peer.sin_port,false,false):ble_owner);
    xSemaphoreGive(peer_mutex);
    if (!available) return V_VOICE_NOT_SUBMITTED;
    uint8_t packet[VIBE_VOICE_BODY+1];
    size_t size=vibe_voice_datagram(type,bytes,length,packet,sizeof(packet));
    if (!size) return V_VOICE_NOT_SUBMITTED;
    if (leg==1) {
        int result=mode==V_VOICE_STREAM ?
            vibe_ble_voice_submit(ntohs(destination.sin_port)-1,(unsigned char)type,packet+1,size-1,ticket->session,ticket->sequence):
            vibe_ble_send_to(ntohs(destination.sin_port)-1,(unsigned char)type,packet+1,size-1);
        return result==0 ? V_VOICE_SUBMITTED:result>0 ? V_VOICE_PARTIAL:V_VOICE_NOT_SUBMITTED;
    }
    if (sendto(udp,packet,size,MSG_DONTWAIT,(struct sockaddr *)&destination,sizeof(destination))==(int)size) return V_VOICE_SUBMITTED;
    *network_error=errno;
    return V_VOICE_NOT_SUBMITTED;
}
/* Credit and lifecycle events must survive a one-way failure of either link.
 * No ownership mutex is held across either networking implementation. */
static void send_json_both(const char *json,uint32_t expected_owner) {
    size_t n=strlen(json);if (n>VIBE_JSON_MAX || !peer_mutex) return;
    struct sockaddr_in wifi,ble;bool has_ble;
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    if (expected_owner && (expected_owner!=owner_generation || release_after_send)) {
        xSemaphoreGive(peer_mutex);return;
    }
    wifi=peer;ble=ble_peer;has_ble=ble_owner;xSemaphoreGive(peer_mutex);
    if (has_ble) (void)vibe_ble_send_to(ntohs(ble.sin_port)-1,2,json,n);
    if (wifi.sin_addr.s_addr && udp>=0) {
        uint8_t packet[VIBE_JSON_MAX+1];packet[0]=2;memcpy(packet+1,json,n);
        (void)sendto(udp,packet,n+1,MSG_DONTWAIT,(struct sockaddr *)&wifi,sizeof(wifi));
    }
}

void vibe_network_radio_activity(bool active) {
    if (atomic_exchange_explicit(&radio_streaming,active,memory_order_relaxed)!=active)
        vibe_network_wake();
}
static void apply_wifi_power(void) {
    bool radio=atomic_load_explicit(&radio_streaming,memory_order_relaxed);
    /* One owner combines radio and the complete voice tail. An audio worker
     * finishing capture must not downgrade BLE while repairs are still live. */
    vibe_ble_streaming(voice_streaming || radio);
    vibe_wifi_power_action_t action=vibe_wifi_power_next(&wifi_power,wifi_started,ap_started,voice_streaming,radio);
    if (action==V_WIFI_POWER_UNCHANGED) return;
    esp_err_t result=esp_wifi_set_ps(action==V_WIFI_POWER_AWAKE?WIFI_PS_NONE:WIFI_PS_MIN_MODEM);
    if (result!=ESP_OK) ESP_LOGW(TAG,"Wi-Fi power transition failed: %s",esp_err_to_name(result));
}
static void voice_activity(bool active) {
    voice_streaming=active;
    vibe_voice_power(active);
    apply_wifi_power();
}
static void service_voice(void) {
    apply_wifi_power();
    /* A ready acknowledgement proves start reached the receiver. Old peers
     * have no acknowledgement: retain their control-before-audio ordering. */
    bool may_transmit=vibe_voice_uplink_can_overtake_control() || !uxQueueMessagesWaiting(tx_queue);
    vibe_voice_uplink_service(may_transmit);
}

/* The sole network sender yields to voice between control submissions. Bound
 * each control batch so producers cannot starve inbound readiness/repair data. */
static bool send_outbound(void) {
    vibe_outbound_t outbound;
    service_voice();
    for (unsigned sent=0;sent<4 && xQueueReceive(tx_queue,&outbound,0)==pdTRUE;sent++) {
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        bool current=vibe_outbound_current(&outbound,owner_generation,release_after_send);
        xSemaphoreGive(peer_mutex);
        if (!current) {free(outbound.json);continue;}
        char *tx=outbound.json;
        if (outbound.delivery!=V_DELIVERY_CRITICAL) {
            if (outbound.delivery==V_DELIVERY_BOTH) send_json_both(tx,outbound.owner);
            else send_packet(2,tx,strlen(tx),outbound.owner);
            free(tx);
            service_voice();
            continue;
        }
        cJSON *o=cJSON_Parse(tx);free(tx);
        if (!o) continue;
        if (!cJSON_AddNumberToObject(o,"eid",++event_sequence)) {cJSON_Delete(o);continue;}
        char *line=cJSON_PrintUnformatted(o);cJSON_Delete(o);
        if (line) {
            /* Mirrors share one identity; voice gets an opportunity between
             * copies, not only after all three bounded BLE sends return. */
            for (unsigned i=0;i<3;i++) {
                send_json_both(line,outbound.owner);
                service_voice();
            }
            free(line);
        }
    }
    return uxQueueMessagesWaiting(tx_queue)==0;
}

static void ui_provision(int phase) {
    vibe_msg_t m={.type=V_MSG_PROVISION,.event=phase}; xQueueSend(v_queue,&m,pdMS_TO_TICKS(100));
}
bool vibe_provision_start(void) { int cmd=1;bool queued=xQueueSend(net_commands,&cmd,0)==pdTRUE;vibe_network_wake();return queued; }
bool vibe_provision_finish(void) { int cmd=2;bool queued=xQueueSend(net_commands,&cmd,0)==pdTRUE;vibe_network_wake();return queued; }
bool vibe_provision_connected(void) { int cmd=6;bool queued=xQueueSend(net_commands,&cmd,0)==pdTRUE;vibe_network_wake();return queued; }
static void connection_changed(void) {
    vibe_msg_t message={.type=V_MSG_CONNECTION_CHANGED};(void)xQueueSend(v_queue,&message,0);
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_START) {
        int command=5;(void)xQueueSend(net_commands,&command,0);vibe_network_wake();
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ip=data;
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        v_status.wifi=true;
        snprintf(v_status.ip,sizeof(v_status.ip),IPSTR,IP2STR(&ip->ip_info.ip));
        xSemaphoreGive(v_mutex);
        connection_changed();
    } else if (base==WIFI_EVENT && (id==WIFI_EVENT_STA_DISCONNECTED || id==WIFI_EVENT_STA_STOP)) {
        const wifi_event_sta_disconnected_t *reason=id==WIFI_EVENT_STA_DISCONNECTED?data:NULL;
        wifi_ap_record_t linked;
        bool associated=esp_wifi_sta_get_ap_info(&linked)==ESP_OK;
        xSemaphoreTake(profiles_mutex,portMAX_DELAY);
        bool relevant=vibe_wifi_disconnect_relevant(active_ssid,reason?reason->ssid:NULL,
            reason?reason->ssid_len:0,associated);
        xSemaphoreGive(profiles_mutex);
        if (!relevant) return;
        xSemaphoreTake(peer_mutex,portMAX_DELAY);bool keep_ble=ble_owner;
        memset(&peer,0,sizeof(peer));wifi_seen=0;
        if (!keep_ble) { last_sent_ms=0;owner_seen=0; }
        xSemaphoreGive(peer_mutex);
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        v_status.wifi=false;
        if (!keep_ble) { v_status.peer=false;v_status.peer_seen_ms=0;v_status.audio_ready=false;v_status.hotkey_ready=false; }
        v_status.signal=(vibe_signal_t){0};
        xSemaphoreGive(v_mutex);
        /* Intentional disconnects precede set_config/connect; do not mistake
         * their delayed callbacks for failure of the replacement attempt. */
        connection_changed();
        if (id==WIFI_EVENT_STA_DISCONNECTED && (!reason || reason->reason!=WIFI_REASON_ASSOC_LEAVE)) {
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
    esp_err_t e=httpd_resp_sendstr(r,s);free(s);
    ESP_LOGI(TAG,"Setup JSON: result=%s heap=%u largest=%u stack=%u",esp_err_to_name(e),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return e;
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
    extern const unsigned char portal_gzip_start[] asm("_binary_vibe_portal_gzip_start");
    extern const unsigned char portal_gzip_end[] asm("_binary_vibe_portal_gzip_end");
    char encodings[128];
    bool compressed=httpd_req_get_hdr_value_str(r,"Accept-Encoding",encodings,sizeof(encodings))==ESP_OK &&
        vibe_portal_accepts_gzip(encodings);
    httpd_resp_set_hdr(r,"Vary","Accept-Encoding");
    if (compressed) httpd_resp_set_hdr(r,"Content-Encoding","gzip");
    const unsigned char *begin=compressed?portal_gzip_start:provisioning_html_start;
    const unsigned char *end=compressed?portal_gzip_end:provisioning_html_end;
    esp_err_t result=httpd_resp_send(r,(const char *)begin,end-begin);
    ESP_LOGI(TAG,"Setup page: result=%s heap=%u largest=%u stack=%u",esp_err_to_name(result),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return result;
}
static esp_err_t setup_status(httpd_req_t *r) {
    if (!local_setup_request(r)) return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Use device Wi-Fi");
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    bool connected=v_status.wifi;setup_phase_t phase=setup_phase;
    char notice[sizeof(v_status.notice)];strlcpy(notice,v_status.notice,sizeof(notice));
    xSemaphoreGive(v_mutex);
    cJSON *o=cJSON_CreateObject();
    cJSON_AddBoolToObject(o,"connected",connected && phase==SETUP_CONNECTED);
    cJSON_AddBoolToObject(o,"connecting",phase==SETUP_CONNECTING);
    cJSON_AddBoolToObject(o,"failed",phase==SETUP_FAILED);
    cJSON_AddStringToObject(o,"message",notice); return json_response(r,o);
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
static void network_ble_disconnected(uint16_t handle);
static esp_err_t setup_pause_ble(void) {
    esp_err_t result=vibe_ble_pause();if (result!=ESP_OK) return result;
    /* The host has joined, so every queued BLE item belongs to the stopped
     * transport. Drain before handles can be reused on resume. */
    vibe_ble_ingress_t *rx;
    while (xQueueReceive(ble_rx_queue,&rx,0)==pdTRUE) free(rx);
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    uint16_t handle=ble_owner?(uint16_t)(ntohs(ble_peer.sin_port)-1):UINT16_MAX;
    xSemaphoreGive(peer_mutex);
    if (handle!=UINT16_MAX) network_ble_disconnected(handle);
    return ESP_OK;
}
#include "setup_lifecycle.inc"
typedef struct {
    bool paired,wifi,busy;
    uint32_t revision;
    unsigned heartbeat;
    uint64_t seen;
    char device[18],ip[16];
} link_snapshot_t;
static link_snapshot_t link_snapshot(void) {
    link_snapshot_t s;
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    s.paired=v_computers.count>0 || v_legacy_computer;s.wifi=v_status.wifi;s.revision=v_status.settings_revision;
    s.heartbeat=v_status.heartbeat_ms;s.seen=v_status.peer_seen_ms;
    s.busy=s.seen && vibe_now()-s.seen<vibe_peer_timeout(s.heartbeat);
    strlcpy(s.device,v_status.device_id,sizeof(s.device));strlcpy(s.ip,v_status.ip,sizeof(s.ip));
    xSemaphoreGive(v_mutex);return s;
}
int vibe_discovery_json(char *buffer,size_t capacity,bool authorized,bool auth_checked,bool trusted) {
    link_snapshot_t s=link_snapshot();
    int n=snprintf(buffer,capacity,"{\"event\":\"device.hello\",\"proto\":2,\"device\":\"%s\",\"paired\":%s,\"authorized\":%s,\"trusted\":%s,\"authChecked\":%s,\"busy\":%s,\"boot\":%lu,\"uptimeMs\":%llu,\"settingsRevision\":%lu,\"clientPort\":true,\"statusBeacon\":true,\"agentNotify\":1,\"agentQuota\":1,\"idleHeartbeat\":30,\"powerDiagnostics\":1,\"selection\":true,\"connectionApproval\":1,\"dualLink\":true,\"voiceV2\":1,\"inputCommands\":1,\"radioPCM\":1,\"radioFormats\":2,\"weatherCache\":1,\"wifi\":%s,\"ip\":\"%s\",\"ble\":true}",s.device,s.paired?"true":"false",authorized?"true":"false",trusted?"true":"false",auth_checked?"true":"false",s.busy?"true":"false",(unsigned long)boot_nonce,(unsigned long long)vibe_now(),(unsigned long)s.revision,s.wifi?"true":"false",s.wifi?s.ip:"");
    return n>0 && (size_t)n<capacity ? n : 0;
}
static void hello_to(const struct sockaddr_in *target,bool via_ble,bool authorized,bool auth_checked,bool trusted) {
    char line[512];int n=vibe_discovery_json(line+1,sizeof(line)-1,authorized,auth_checked,trusted);line[0]=2;
    if (!n) return;
    if (via_ble) (void)vibe_ble_send_to(ntohs(target->sin_port)-1,2,line+1,n);
    else if (target && target->sin_port) sendto(udp,line,n+1,MSG_DONTWAIT,(const struct sockaddr *)target,sizeof(*target));
}
#include "link_receive.inc"
void vibe_receive_ble(uint16_t handle,uint32_t generation,const void *packet,size_t n) {
    (void)vibe_ble_ingress_post(ble_rx_queue,handle,generation,packet,n);
}
void vibe_ble_disconnected(uint16_t handle) {
    /* Host callbacks cannot take peer_mutex/v_mutex: sending takes NimBLE's
     * host lock in the opposite direction. Process teardown on the worker. */
    (void)vibe_ble_ingress_disconnect(ble_rx_queue,handle);
}
static void network_ble_disconnected(uint16_t handle) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool owned=ble_owner && ntohs(ble_peer.sin_port)==handle+1;
    if (owned) { ble_owner=false;ble_seen=0;memset(&ble_peer,0,sizeof(ble_peer)); }
    bool keep_wifi=vibe_peer_use_wifi(vibe_now(),wifi_seen,0,peer.sin_addr.s_addr && peer.sin_port,false,false);
    /* A quiet Wi-Fi path can still have a valid idle-heartbeat lease. Only
     * release immediately when BLE was the owner's sole attachment. */
    if (owned && !peer.sin_port) owner_seen=0;
    xSemaphoreGive(peer_mutex);
    if (owned) {
        xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.ble_peer=false;
        if (!keep_wifi) { v_status.peer=false;v_status.peer_seen_ms=0;v_status.audio_ready=false;v_status.hotkey_ready=false; }
        xSemaphoreGive(v_mutex);
        connection_changed();
    }
}
#include "link_release.inc"
static void network_task(void *arg) {
    (void)arg;
    udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_IP);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(VIBE_PORT),.sin_addr.s_addr=INADDR_ANY};
    struct timeval timeout={.tv_sec=0,.tv_usec=100000};
    if (udp<0 || bind(udp,(struct sockaddr *)&address,sizeof(address))<0) {
        ESP_LOGE(TAG,"Cannot bind voice port");if (udp>=0) close(udp);udp=-1;vTaskDelete(NULL);return;
    }
    setsockopt(udp,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    /* VI access class for bounded live media/control. Bulk HTTP traffic uses
     * separate sockets. Unsupported QoS leaves ordinary UDP functional. */
    int priority=4<<5;
    if (setsockopt(udp,IPPROTO_IP,IP_TOS,&priority,sizeof(priority))<0)
        ESP_LOGW(TAG,"UDP priority unavailable: %d",errno);
    uint64_t signal_at=0;
    bool connected_handled=false;
    vibe_clock_sync_t clock_sync={0};
    vibe_wifi_retry_reset(&auto_connection,vibe_now());

    for (;;) {
        uint64_t now=vibe_now(); int cmd;
        if (!ap_started && setup_ble_paused && now>=ble_resume_at) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);bool busy=v_model.recording || v_status.audio_busy;xSemaphoreGive(v_mutex);
            if (!busy) setup_restore_ble(now);
        }
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        bool clock_wifi=v_status.wifi,clock_paused=v_model.recording || v_status.provisioning;
        xSemaphoreGive(v_mutex);
        vibe_clock_poll(&clock_sync,now,clock_wifi,clock_paused);
        vibe_weather_poll(now);
        while (xQueueReceive(net_commands,&cmd,0)==pdTRUE) {
            if (cmd==1) start_ap();
            else if (cmd==2) {
                if (testing_credentials) { testing_credentials=false;memset(&candidate,0,sizeof(candidate));vibe_wifi_retry_reset(&auto_connection,now); }
                finish_ap(false);
            } else if (cmd==6) {
                /* A newly paired BLE computer can finish first-use setup,
                 * but must not abort a submitted Wi-Fi connection attempt. */
                if (!testing_credentials) finish_ap(false);
            }
            else if (cmd==3) {
                if (!ap_started) { memset(&candidate,0,sizeof(candidate));continue; }
                setup_saved_at=0;
                xSemaphoreTake(v_mutex,portMAX_DELAY);
                v_status.wifi=false; strlcpy(v_status.notice,"正在连接 Wi-Fi",sizeof(v_status.notice));
                xSemaphoreGive(v_mutex);
                testing_credentials=true; credentials_at=now;
                auto_connection.pending=false;(void)connect_network(&candidate);
            } else if (cmd==5 && !testing_credentials && !ap_started) {
                vibe_wifi_retry_reset(&auto_connection,now);
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
        vibe_status_t s; vibe_snapshot(NULL,NULL,&s);
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
        } else if (!ap_started) {
            xSemaphoreTake(profiles_mutex,portMAX_DELAY);unsigned count=networks.count;
            xSemaphoreGive(profiles_mutex);
            wifi_ap_record_t associated_ap;
            bool associated=auto_connection.pending && esp_wifi_sta_get_ap_info(&associated_ap)==ESP_OK;
            if (vibe_wifi_retry_timeout(&auto_connection,now,associated)) { vibe_wifi_retry_failed(&auto_connection,now,count);esp_wifi_disconnect(); }
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
            } else if (vibe_portal_close_due(s.wifi,setup_saved_at,now)) finish_ap(true);
        }
        char metadata[512];int metadata_size=vibe_discovery_json(metadata,sizeof(metadata),false,false,false);
        if (metadata_size>0) vibe_ble_publish_metadata(metadata,metadata_size);
        static unsigned char packet[VIBE_JSON_MAX+2]; struct sockaddr_in from; socklen_t len=sizeof(from);
        vibe_model_t power;vibe_snapshot(&power,NULL,NULL);
        unsigned wait_ms=(s.audio_busy && power.radio_active && power.radio_requested)?20:ap_started?100:power.recording?20:power.screen_on?200:
            (!s.wifi || testing_credentials || auto_connection.pending)?1000:30000;
        /* BLE host callbacks only enqueue; a bounded poll avoids calling into
         * lwIP from inside NimBLE or waiting 30 seconds for the first packet. */
        if (wait_ms>250) wait_ms=250;
        wait_ms=vibe_voice_uplink_wait(vibe_clock_wait(&clock_sync,vibe_now(),wait_ms));
        timeout.tv_usec=(wait_ms%1000)*1000;timeout.tv_sec=wait_ms/1000;
        int ready=VIBE_PORTAL_DATA_READY;
        if (dns_socket>=0) {
            ready=vibe_portal_wait(udp,dns_socket,wait_ms);
            if (ready>0 && (ready&VIBE_PORTAL_DNS_READY)) vibe_portal_dns_poll(dns_socket);
        } else setsockopt(udp,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        int n=ready>0 && (ready&VIBE_PORTAL_DATA_READY)?
            recvfrom(udp,packet,sizeof(packet)-1,dns_socket>=0?MSG_DONTWAIT:0,(struct sockaddr *)&from,&len):0;
        now=vibe_now(); /* A long blocking receive must not age fresh beacons. */
        if (n>1) process_packet(packet,n,&from,false);
        service_voice();
        vibe_ble_ingress_t *rx;
        for (unsigned received=0;received<4 && xQueueReceive(ble_rx_queue,&rx,0)==pdTRUE;received++) {
            struct sockaddr_in source={.sin_family=AF_INET,.sin_port=htons(rx->handle+1),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
            if (!rx->length) network_ble_disconnected(rx->handle);
            else {
                bool current=vibe_ble_generation_matches(rx->handle,rx->generation);
                static unsigned received_debug;
                if (received_debug++<4) ESP_LOGI("vibe_ble","Worker ingress bytes=%u current=%d",(unsigned)rx->length,current);
                if (current) process_packet(rx->bytes,rx->length,&source,true);
            }
            free(rx);
            service_voice();
        }

        bool control_drained=send_outbound();
        service_voice();
        /* Preserve the queued disconnect/unpair reply across bounded turns. */
        if (control_drained) finish_release();
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        uint64_t sent=last_sent_ms;bool has_peer=(peer.sin_addr.s_addr && peer.sin_port) || ble_owner;
        xSemaphoreGive(peer_mutex);
        if ((s.wifi || s.ble_peer) && vibe_peer_heartbeat_due(vibe_now(),sent,s.peer_seen_ms,has_peer,s.heartbeat_ms)) send_packet(4,"",0,0);
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
    boot_nonce=esp_random();
    peer_mutex=xSemaphoreCreateMutex();profiles_mutex=xSemaphoreCreateMutex(); tx_queue=xQueueCreate(8,sizeof(vibe_outbound_t)); net_commands=xQueueCreate(4,sizeof(int));ble_rx_queue=xQueueCreate(4,sizeof(vibe_ble_ingress_t *));
    if (!peer_mutex || !profiles_mutex || !tx_queue || !net_commands || !ble_rx_queue) return ESP_ERR_NO_MEM;
    esp_err_t voice_result=vibe_voice_uplink_init((vibe_voice_uplink_ports_t){
        .owner=vibe_network_generation,.links=voice_transport_links,
        .send=voice_transport_send,.wake=vibe_network_wake,.activity=voice_activity,
        .acquire=vibe_media_voice_acquire,.release=vibe_media_voice_release});
    if (voice_result!=ESP_OK) return voice_result;
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta(); esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    wake_mutex=xSemaphoreCreateMutex();
    if (wake_mutex) wake_udp=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);
    profiles_load();
    esp_wifi_set_mode(WIFI_MODE_STA);
    /* A blank device starts with BLE discovery. Start Wi-Fi only for saved
     * networks or an explicit Wi-Fi setup request; never force the portal. */
    if (networks.count) { ESP_ERROR_CHECK(esp_wifi_start());wifi_started=true; }
    if (xTaskCreate(network_task,"vibe_net",8192,NULL,4,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
