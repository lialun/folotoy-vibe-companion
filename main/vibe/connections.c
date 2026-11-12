#include "vibe.h"
#include "connections.h"
#include "transport.h"
#include "ble.h"
#include "diagnostics.h"
#include "radio/catalog_service.h"
#include "nvs.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static vibe_hosts_t hosts;
static uint32_t boot;
static uint32_t published_lease;
static uint64_t pair_attempt;
static const char *string(const cJSON *o,const char *key) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(j)?j->valuestring:"";
}
static bool generation_matches(const cJSON *o,const char *key,uint32_t value) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);
    return cJSON_IsNumber(j) && j->valuedouble==(double)value;
}
static bool persist(const vibe_hosts_saved_t *saved) {
    nvs_handle_t n;if (nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) return false;
    bool ok=nvs_set_blob(n,"hosts",saved,sizeof(*saved))==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);return ok;
}
void vibe_connections_init(const char *legacy) {
    vibe_hosts_saved_t saved;size_t size=sizeof(saved);nvs_handle_t n;bool loaded=false;
    v_status.wifi_enabled=true;v_status.ble_enabled=true;boot=vibe_transport_boot();
    if (nvs_open("vibe",NVS_READONLY,&n)==ESP_OK) {
        loaded=nvs_get_blob(n,"hosts",&saved,&size)==ESP_OK && vibe_hosts_decode(&saved,&saved,size);
        uint8_t flags=3;if (nvs_get_u8(n,"wireless",&flags)==ESP_OK && flags<=3) { v_status.wifi_enabled=flags&1;v_status.ble_enabled=flags&2; }
        nvs_close(n);
    }
    vibe_hosts_init(&hosts,loaded?&saved:NULL,legacy);
    v_status.paired_count=hosts.saved.count;
}
static void respond(cJSON *o,vibe_route_t route) {
    char *line=cJSON_PrintUnformatted(o);if (line) { vibe_transport_reply(line,route);free(line); }cJSON_Delete(o);
}
static void status(vibe_route_t route,int host,const char *id,const char *error) {
    cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"event","device.hello");
    cJSON_AddNumberToObject(o,"proto",3);cJSON_AddStringToObject(o,"device",v_status.device_id);
    cJSON_AddBoolToObject(o,"paired",host>=0);cJSON_AddNumberToObject(o,"boot",boot);
    cJSON_AddNumberToObject(o,"lease",hosts.lease);cJSON_AddBoolToObject(o,"owned",host>=0 && host==hosts.owner);
    cJSON_AddStringToObject(o,"ownerId",host>=0 && hosts.owner>=0?hosts.saved.hosts[hosts.owner].id:"");
    cJSON_AddStringToObject(o,"ownerName",host>=0 && hosts.owner>=0?hosts.saved.hosts[hosts.owner].name:"");
    cJSON_AddStringToObject(o,"transport",hosts.selected==V_LINK_WIFI?"wifi":hosts.selected==V_LINK_BLE?"ble":"");
    cJSON_AddNumberToObject(o,"settingsRevision",v_status.settings_revision);
    cJSON_AddBoolToObject(o,"clientPort",true);cJSON_AddBoolToObject(o,"statusBeacon",true);
    cJSON_AddNumberToObject(o,"radioRelay",1);cJSON_AddNumberToObject(o,"agentNotify",1);cJSON_AddNumberToObject(o,"idleHeartbeat",30);cJSON_AddNumberToObject(o,"powerDiagnostics",1);
    cJSON_AddNumberToObject(o,"diagnostics",1);cJSON_AddNumberToObject(o,"diagnosticSeq",vibe_diagnostics_sequence());
    cJSON_AddBoolToObject(o,"wifiEnabled",v_status.wifi_enabled);cJSON_AddBoolToObject(o,"bleEnabled",v_status.ble_enabled);
    if (id && *id) { cJSON_AddStringToObject(o,"id",id);cJSON_AddBoolToObject(o,"ok",!error); }
    if (error) cJSON_AddStringToObject(o,"error",error);
    respond(o,route);
}
void vibe_connections_poll(uint64_t now) {
    vibe_hosts_poll(&hosts,now);vibe_host_link_t current=vibe_hosts_active(&hosts,now);
    vibe_host_link_t standby=hosts.owner>=0?hosts.links[hosts.owner][1]:(vibe_host_link_t){0};
    v_status.ble_peer=standby.route.kind && now>=standby.seen && now-standby.seen<standby.timeout_ms?standby.route:(vibe_route_t){0};
    v_status.peer=current.route.kind!=V_LINK_NONE;
    v_status.audio_ready=current.audio;v_status.hotkey_ready=current.hotkey;
    v_status.peer_seen_ms=current.seen;v_status.link=current.route.kind;
    snprintf(v_status.computer,sizeof(v_status.computer),"%s",hosts.owner>=0?hosts.saved.hosts[hosts.owner].name:"");
    if (published_lease!=hosts.lease) {
        published_lease=hosts.lease;
        /* Voice is bound to its starting lease. A handoff ends it, never sends
         * the remaining PCM or queued Enter to the replacement computer. */
        if (v_model.recording) { v_model.recording=false;vibe_audio_request(V_STOP_AUDIO); }
        vibe_radio_cancel_transfer();
        vibe_transport_owner(current.route,hosts.lease);
        if (v_model.radio_active && v_model.radio_requested) (void)vibe_audio_request(V_RADIO_CHANGED);
        for (unsigned i=0;i<hosts.saved.count;i++) for (unsigned j=0;j<2;j++) {
            vibe_host_link_t l=hosts.links[i][j];
            if (l.route.kind && now>=l.seen && now-l.seen<l.timeout_ms) status(l.route,(int)i,NULL,NULL);
        }
    }
}
void vibe_connections_lost(vibe_route_t r) { vibe_hosts_lost(&hosts,r);vibe_connections_poll(vibe_now()); }
void vibe_connections_wifi_lost(void) { vibe_hosts_disable(&hosts,V_LINK_WIFI);vibe_connections_poll(vibe_now()); }
bool vibe_connections_clear(void) {
    vibe_hosts_saved_t empty={.version=1};if (!persist(&empty)) return false;
    nvs_handle_t n;if (nvs_open("vibe",NVS_READWRITE,&n)==ESP_OK) { nvs_erase_key(n,"token");nvs_commit(n);nvs_close(n); }
    vibe_hosts_init(&hosts,&empty,NULL);hosts.lease=++published_lease;vibe_transport_owner((vibe_route_t){0},hosts.lease);
    v_status.paired_count=0;v_status.token[0]=0;v_status.peer=false;v_status.audio_ready=false;v_status.hotkey_ready=false;
    snprintf(v_status.pair_code,sizeof(v_status.pair_code),"%06lu",(unsigned long)(esp_random()%1000000));return true;
}
uint32_t vibe_connections_lease(void) { return hosts.lease; }
bool vibe_connections_wireless(bool wifi,bool ble) {
    nvs_handle_t n;if (nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) return false;
    bool ok=nvs_set_u8(n,"wireless",(wifi?1:0)|(ble?2:0))==ESP_OK && nvs_commit(n)==ESP_OK;nvs_close(n);
    if (ok) {
        v_status.wifi_enabled=wifi;v_status.ble_enabled=ble;
        v_model.wifi_enabled=wifi;v_model.ble_enabled=ble;vibe_network_wake();

    }
    return ok;
}
bool vibe_connections_receive(const cJSON *o,vibe_route_t source,const char *id) {
    const char *type=string(o,"type"),*client=string(o,"client"),*token=string(o,"token"),*name=string(o,"name");
    uint64_t now=vibe_now();int host=vibe_hosts_auth(&hosts,client,token);
    if (!source.kind) return false;
    if (!strcmp(type,"discover") || !strcmp(type,"pc.status")) {
        if (host>=0) {
            if ((!hosts.saved.hosts[host].id[0] || strcmp(hosts.saved.hosts[host].name,name)) && name[0]) {
                vibe_hosts_saved_t next=hosts.saved;
                if (vibe_hosts_register(&next,client,name,token,host)<0 || !persist(&next)) { status(source,-1,id,"无法保存电脑身份");return false; }
                hosts.saved=next;
            }
            const cJSON *interval=cJSON_GetObjectItemCaseSensitive(o,"idleHeartbeat");
            unsigned timeout=cJSON_IsNumber(interval) && interval->valuedouble>=5 && interval->valuedouble<=30 ? (unsigned)interval->valueint*3000:15000;
            vibe_hosts_seen(&hosts,host,source,now,timeout,cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"audioReady")),cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"hotkeyReady")));
            vibe_connections_poll(now);
        }
        status(source,host,id,NULL);
        /* Only the active status may set device time and readiness. */
        return !strcmp(type,"pc.status") && vibe_hosts_controls(&hosts,host,source,now);
    }
    if (!strcmp(type,"pair")) {
        if (pair_attempt && now-pair_attempt<1000) return false;
        pair_attempt=now;
        if (host<0 && strcmp(string(o,"code"),v_status.pair_code)) { status(source,-1,id,"配对码不正确");return false; }
        vibe_hosts_saved_t next=hosts.saved;
        int slot=host>=0?vibe_hosts_register(&next,client,name,token,host):vibe_hosts_pair(&next,client,name,token);
        if (slot<0 || !persist(&next)) { status(source,host,id,"无法配对，请检查电脑名称或解除不再使用的配对");return false; }
        hosts.saved=next;v_status.paired_count=next.count;
        vibe_hosts_seen(&hosts,slot,source,now,30000,false,false);vibe_connections_poll(now);
        status(source,slot,id,NULL);snprintf(v_status.pair_code,sizeof(v_status.pair_code),"%06lu",(unsigned long)(esp_random()%1000000));return false;
    }
    if (host<0) return false;
    /* Leases can repeat after reboot. Bind every control, including takeover,
     * to both the boot nonce and generation rather than just the credential. */
    bool same_boot=generation_matches(o,"boot",boot);
    bool same_lease=generation_matches(o,"lease",hosts.lease);
    if (!strcmp(type,"peer.claim")) {
        /* A retry by the current owner is harmless even if its success reply
         * was lost. A delayed claim from a replaced owner must never preempt. */
        bool ok=same_boot && (same_lease || host==hosts.owner) && vibe_hosts_claim(&hosts,host,source,now);
        vibe_connections_poll(now);
        status(source,host,id,ok?NULL:"请重新连接后切换");return false;
    }
    bool allowed=same_boot && same_lease && vibe_hosts_controls(&hosts,host,source,now);
    if (!allowed) { status(source,host,id,"请先切换到本机");return false; }
    if (!strcmp(type,"wireless.set")) {
        const cJSON *w=cJSON_GetObjectItemCaseSensitive(o,"wifiEnabled"),*b=cJSON_GetObjectItemCaseSensitive(o,"bleEnabled");
        bool ok=cJSON_IsBool(w) && cJSON_IsBool(b) && !v_status.audio_busy;
        if (ok) ok=vibe_connections_wireless(cJSON_IsTrue(w),cJSON_IsTrue(b));
        status(source,host,id,ok?NULL:"请停止音频后重试");return false;
    }
    return true;
}
