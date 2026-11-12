#include "../../main/vibe/connections.c"
#include <assert.h>
vibe_model_t v_model;
vibe_prefs_t v_prefs;
vibe_status_t v_status;
static uint64_t now=2000;
static bool fail_save,has_saved;
static vibe_hosts_saved_t stored;
static uint8_t wireless=3;
static vibe_route_t reply_to,selected;
static char *reply;
static unsigned stopped;
uint32_t vibe_diagnostics_sequence(void) { return 0; }
void vibe_radio_cancel_transfer(void) {}
uint64_t vibe_now(void) { return now; }
uint32_t vibe_transport_boot(void) { return 10; }
uint32_t esp_random(void) { return 123456; }
void vibe_transport_reply(const char *json,vibe_route_t route) { free(reply);reply=strdup(json);reply_to=route; }
void vibe_transport_owner(vibe_route_t route,uint32_t lease) { (void)lease;selected=route; }
void vibe_network_wake(void) {}
bool vibe_audio_request(unsigned action) { assert(action==V_STOP_AUDIO || action==V_RADIO_CHANGED);stopped++;return true; }
esp_err_t nvs_open(const char *name,unsigned mode,nvs_handle_t *n) { (void)name;(void)mode;*n=1;return ESP_OK; }
void nvs_close(nvs_handle_t n) { (void)n; }
esp_err_t nvs_get_blob(nvs_handle_t n,const char *key,void *out,size_t *size) {
    (void)n;assert(!strcmp(key,"hosts"));if (!has_saved) return ESP_FAIL;
    assert(*size>=sizeof(stored));memcpy(out,&stored,sizeof(stored));*size=sizeof(stored);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t n,const char *key,const void *bytes,size_t size) {
    (void)n;assert(!strcmp(key,"hosts") && size==sizeof(stored));if (fail_save) return ESP_FAIL;
    memcpy(&stored,bytes,size);has_saved=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t n) { (void)n;return fail_save?ESP_FAIL:ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t n,const char *key,uint8_t *out) { (void)n;(void)key;*out=wireless;return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t n,const char *key,uint8_t value) { (void)n;(void)key;wireless=value;return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t n,const char *key) { (void)n;(void)key;return ESP_OK; }
static const char *ids[]={"11111111111111111111111111111111","22222222222222222222222222222222"};
static const char *tokens[]={"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
static bool receive(unsigned computer,const char *type,vibe_route_t route,bool valid_code,bool current) {
    cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"type",type);cJSON_AddStringToObject(o,"client",ids[computer]);
    cJSON_AddStringToObject(o,"name","Same name");cJSON_AddStringToObject(o,"token",tokens[computer]);
    cJSON_AddStringToObject(o,"code",valid_code?v_status.pair_code:"000000");
    cJSON_AddBoolToObject(o,"audioReady",true);cJSON_AddBoolToObject(o,"hotkeyReady",true);cJSON_AddNumberToObject(o,"idleHeartbeat",10);
    cJSON_AddNumberToObject(o,"boot",boot);cJSON_AddNumberToObject(o,"lease",current?hosts.lease:hosts.lease-1);
    bool ok=vibe_connections_receive(o,route,"test");cJSON_Delete(o);return ok;
}
int main(void) {
    vibe_route_t a={.kind=V_LINK_WIFI,.address=1,.port=40000},ab={.kind=V_LINK_BLE,.handle=1,.serial=1},b={.kind=V_LINK_BLE,.handle=2,.serial=2};
    strcpy(v_status.device_id,"123456789abc");strcpy(v_status.pair_code,"123456");
    vibe_connections_init(tokens[0]);assert(v_status.paired_count==1);
    fail_save=true;assert(!receive(0,"pc.status",a,false,true));assert(hosts.owner==-1 && !has_saved);
    fail_save=false;assert(receive(0,"pc.status",a,false,true));assert(has_saved && hosts.owner==0);
    assert(!strcmp(stored.hosts[0].id,ids[0]));assert(vibe_route_equal(selected,a));
    assert(!receive(0,"pc.status",ab,false,true));assert(hosts.selected==V_LINK_WIFI);
    assert(!receive(1,"pair",b,false,true));assert(hosts.saved.count==1);
    now+=1200;assert(!receive(1,"pair",b,true,true));assert(hosts.saved.count==2 && hosts.owner==0);
    assert(vibe_route_equal(reply_to,b));
    assert(!receive(1,"config.set",b,false,true));assert(!receive(1,"agent.notify",b,false,true));
    v_model.recording=true;assert(!receive(1,"peer.claim",b,false,true));
    assert(hosts.owner==1 && stopped==1 && !v_model.recording && vibe_route_equal(selected,b));
    assert(!receive(0,"config.get",a,false,true));assert(!receive(1,"config.get",b,false,false));
    assert(receive(1,"config.get",b,false,true));
    /* A's previously queued claim cannot undo B's explicit takeover. */
    uint32_t owned_lease=hosts.lease;
    assert(!receive(0,"peer.claim",a,false,false));assert(hosts.owner==1 && hosts.lease==owned_lease);
    /* Lost success replies can be retried by the current owner without changing
     * its route or lease, including a retry carrying the preceding lease. */
    assert(!receive(1,"peer.claim",b,false,false));assert(hosts.owner==1 && hosts.lease==owned_lease);
    uint32_t current_boot=boot;boot++;
    cJSON *stale=cJSON_CreateObject();cJSON_AddStringToObject(stale,"type","config.set");
    cJSON_AddStringToObject(stale,"client",ids[1]);cJSON_AddStringToObject(stale,"token",tokens[1]);
    cJSON_AddNumberToObject(stale,"boot",current_boot);cJSON_AddNumberToObject(stale,"lease",hosts.lease);
    assert(!vibe_connections_receive(stale,b,"old-boot"));
    cJSON_ReplaceItemInObject(stale,"type",cJSON_CreateString("peer.claim"));
    cJSON_ReplaceItemInObject(stale,"client",cJSON_CreateString(ids[0]));
    cJSON_ReplaceItemInObject(stale,"token",cJSON_CreateString(tokens[0]));
    assert(!vibe_connections_receive(stale,a,"old-boot-claim"));assert(hosts.owner==1);
    cJSON_DeleteItemFromObject(stale,"boot");
    assert(!vibe_connections_receive(stale,a,"missing-boot"));assert(hosts.owner==1);
    cJSON_AddBoolToObject(stale,"boot",true);
    assert(!vibe_connections_receive(stale,a,"boolean-boot"));assert(hosts.owner==1);
    cJSON_Delete(stale);boot=current_boot;
    vibe_connections_lost(b);assert(hosts.owner==0 && hosts.selected==V_LINK_WIFI);
    vibe_connections_wifi_lost();assert(hosts.owner==0 && hosts.selected==V_LINK_BLE);
    v_model.radio_active=true;v_model.radio_requested=true;
    assert(vibe_connections_wireless(false,true));assert(wireless==2 && !v_status.wifi_enabled && v_model.ble_enabled);
    assert(v_model.radio_active && v_model.radio_requested && stopped==1);
    vibe_connections_init(NULL);assert(hosts.saved.count==2 && !v_status.wifi_enabled); /* Reboot persists registry/settings. */
    now+=1200;cJSON *repair=cJSON_CreateObject();cJSON_AddStringToObject(repair,"type","pair");
    cJSON_AddStringToObject(repair,"client",ids[0]);cJSON_AddStringToObject(repair,"name","Recovered computer");
    const char *replacement="cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    cJSON_AddStringToObject(repair,"token",replacement);cJSON_AddStringToObject(repair,"code",v_status.pair_code);
    fail_save=true;assert(!vibe_connections_receive(repair,a,"repair-fail"));
    assert(vibe_hosts_auth(&hosts,ids[0],tokens[0])==0 && vibe_hosts_auth(&hosts,ids[0],replacement)<0);
    now+=1200;fail_save=false;assert(!vibe_connections_receive(repair,a,"repair-success"));
    assert(hosts.saved.count==2 && vibe_hosts_auth(&hosts,ids[0],replacement)==0 && vibe_hosts_auth(&hosts,ids[1],tokens[1])==1);
    assert(!strcmp(stored.hosts[0].token,replacement));cJSON_Delete(repair);
    fail_save=true;assert(!vibe_connections_clear() && hosts.saved.count==2);
    fail_save=false;assert(vibe_connections_clear() && hosts.saved.count==0 && !selected.kind);
    free(reply);puts("Controller authentication, persisted migration, two-computer handoff and stale writes: PASS");
}
