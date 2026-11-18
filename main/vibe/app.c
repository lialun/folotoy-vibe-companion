#include "vibe.h"
#include "radio/catalog_service.h"
#include "radio/remote.h"
#include "radio/stream.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "power.h"
#include "peer.h"
#include "ble.h"
#include "driver/usb_serial_jtag.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "key_guard.h"
#include "input_gate.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

vibe_model_t v_model;
vibe_prefs_t v_prefs;
vibe_status_t v_status;
SemaphoreHandle_t v_mutex;
QueueHandle_t v_queue;
static volatile bool input_overflow;
static bool held[3], long_press[3];
static atomic_bool screen_visible;
static vibe_prompt_input_t prompt_input;
static bool intro_skipped;
static key_guard_t wake_guard;
static const char *TAG="vibe";
// Catch a future config change that would silently block automatic light sleep.
_Static_assert(CONFIG_BUTTON_PERIOD_TIME_MS * 1000ULL * CONFIG_FREERTOS_HZ >
    CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP * 1000000ULL,
    "Button polling must leave time for automatic light sleep");
uint64_t vibe_now(void) { return esp_timer_get_time()/1000ULL; }
void vibe_snapshot(vibe_model_t *m, vibe_prefs_t *p, vibe_status_t *s) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    if (m) *m=v_model;
    if (p) *p=v_prefs;
    if (s) *s=v_status;
    xSemaphoreGive(v_mutex);
}
static void load_prefs(void) {
    v_prefs=vibe_prefs_default();v_status.appearance=vibe_appearance_default();
    nvs_handle_t n;
    if (nvs_open("vibe",NVS_READWRITE,&n)==ESP_OK) {
        vibe_saved_prefs_t stored;size_t z=sizeof(stored);
        bool loaded=nvs_get_blob(n,"prefs3",&stored,&z)==ESP_OK &&
            vibe_prefs_decode(&stored,z,false,&v_prefs,&v_status.appearance);
        if (!loaded) {
            vibe_prefs_t legacy;z=sizeof(legacy);
            if (nvs_get_blob(n,"prefs",&legacy,&z)==ESP_OK)
                vibe_prefs_decode(&legacy,z,true,&v_prefs,&v_status.appearance);
        }
        z=sizeof(v_status.token);nvs_get_str(n,"token",v_status.token,&z);
        uint8_t skipped=0;
        if (nvs_get_u8(n,"intro_skipped",&skipped)==ESP_OK) intro_skipped=skipped==1;
        nvs_close(n);
    }
    v_status.token[sizeof(v_status.token)-1]=0;
}
bool vibe_save_prefs(void) {
    nvs_handle_t n;
    if (nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) return false;
    vibe_saved_prefs_t stored={.version=VIBE_PREFS_VERSION,.prefs=v_prefs,.appearance=v_status.appearance};
    bool ok=nvs_set_blob(n,"prefs3",&stored,sizeof(stored))==ESP_OK && nvs_commit(n)==ESP_OK;
    if (!ok) ESP_LOGE(TAG,"Cannot save device settings");
    nvs_close(n);
    if (ok) ++v_status.settings_revision;
    return ok;
}
static void model_from_prefs(void) {
    v_model.brightness=v_prefs.brightness; v_model.volume=v_prefs.volume;
    v_model.screen_seconds=v_prefs.screen_seconds;
    v_model.focus_minutes=v_prefs.focus_minutes; v_model.break_minutes=v_prefs.break_minutes;
    v_model.theme_mode=(vibe_theme_mode_t)v_status.appearance.mode;
}
static void send_object(cJSON *o) {
    char *s=cJSON_PrintUnformatted(o);
    if (s) { vibe_send_json(s); free(s); }
    cJSON_Delete(o);
}
static void dismiss_agent_notice(void) {
    if (!vibe_agent_dismiss(&v_status.agent_alert)) return;
    char json[96];
    snprintf(json,sizeof(json),"{\"event\":\"agent.dismissed\",\"id\":\"%llu\"}",
             (unsigned long long)v_status.agent_alert.active.id);
    vibe_send_json_both(json);
}
static void prefs_message(const char *event,const char *id, const char *error) {
    cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"event",event);
    if (id) cJSON_AddStringToObject(o,"id",id);
    cJSON_AddNumberToObject(o,"settingsRevision",v_status.settings_revision);
    cJSON_AddBoolToObject(o,"ok",!error);
    if (error) cJSON_AddStringToObject(o,"error",error);
    else {
        cJSON *p=cJSON_AddObjectToObject(o,"settings");
#define NUMBER(k) cJSON_AddNumberToObject(p,#k,v_prefs.k)
        NUMBER(brightness); NUMBER(volume); NUMBER(screen_seconds); NUMBER(focus_minutes);
        NUMBER(break_minutes); NUMBER(mic_gain); NUMBER(latitude); NUMBER(longitude);
#undef NUMBER
        cJSON_AddBoolToObject(p,"weather_enabled",v_prefs.weather_enabled);
        cJSON_AddStringToObject(p,"city",v_prefs.city);
        cJSON_AddStringToObject(p,"radio_url",v_prefs.radio_url);
        cJSON_AddStringToObject(p,"radio_name",v_prefs.radio_name);
        cJSON_AddNumberToObject(p,"screen_seconds_min",3);
        cJSON_AddNumberToObject(p,"radio_list_version",1);
        cJSON *a=cJSON_AddObjectToObject(p,"appearance");
        cJSON_AddNumberToObject(a,"mode",v_status.appearance.mode);
        cJSON_AddNumberToObject(a,"light_start",v_status.appearance.light_start);
        cJSON_AddNumberToObject(a,"dark_start",v_status.appearance.dark_start);
    }
    send_object(o);
}
static void prefs_reply(const char *id,const char *error) { prefs_message("config.reply",id,error); }
static bool integer(const cJSON *o,const char *k,unsigned *dst,unsigned lo,unsigned hi) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,k);
    if (!j) return true;
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || j->valuedouble<lo || j->valuedouble>hi || floor(j->valuedouble)!=j->valuedouble) return false;
    *dst=(unsigned)j->valuedouble; return true;
}
static bool text(const cJSON *o,const char *k,char *dst,size_t cap) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,k);
    if (!j) return true;
    if (!cJSON_IsString(j) || strlen(j->valuestring)>=cap || strchr(j->valuestring,'\n') || strchr(j->valuestring,'\r')) return false;
    strcpy(dst,j->valuestring); return true;
}
static bool prefs_parse(const cJSON *o,vibe_prefs_t *p,vibe_appearance_t *a) {
    if (!cJSON_IsObject(o)) return false;
    if (!integer(o,"brightness",&p->brightness,10,100) || !integer(o,"volume",&p->volume,0,100) ||
        !integer(o,"screen_seconds",&p->screen_seconds,3,60) || !integer(o,"mic_gain",&p->mic_gain,0,42) ||
        !integer(o,"focus_minutes",&p->focus_minutes,1,180) || !integer(o,"break_minutes",&p->break_minutes,1,60) ||
        !text(o,"city",p->city,sizeof(p->city)) || !text(o,"radio_url",p->radio_url,sizeof(p->radio_url)) ||
        !text(o,"radio_name",p->radio_name,sizeof(p->radio_name))) return false;
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,"weather_enabled");
    if (j && !cJSON_IsBool(j)) return false; /* Legacy field is accepted; weather is always on. */
    j=cJSON_GetObjectItemCaseSensitive(o,"latitude");
    if (j) { if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || fabs(j->valuedouble)>90) return false; p->latitude=j->valuedouble; }
    j=cJSON_GetObjectItemCaseSensitive(o,"longitude");
    if (j) { if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || fabs(j->valuedouble)>180) return false; p->longitude=j->valuedouble; }
    j=cJSON_GetObjectItemCaseSensitive(o,"appearance");
    if (j) {
        unsigned mode=a->mode,light=a->light_start,dark=a->dark_start;
        if (!cJSON_IsObject(j) || !integer(j,"mode",&mode,0,V_THEME_SCHEDULED) ||
            !integer(j,"light_start",&light,0,1439) || !integer(j,"dark_start",&dark,0,1439)) return false;
        a->mode=mode;a->light_start=light;a->dark_start=dark;
        if (!vibe_appearance_valid(a)) return false;
    }
    vibe_prefs_require_weather(p);
    return (!strncmp(p->radio_url,"http://",7) || !strncmp(p->radio_url,"https://",8)) && strlen(p->radio_url)>8;
}
static bool token_valid(const char *s) {
    if (!s || strlen(s)!=64) return false;
    for (int i=0;i<64;i++) if (!strchr("0123456789abcdef",s[i])) return false;
    return true;
}
static bool forget_computer(void) {
    nvs_handle_t n;
    if (nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) return false;
    esp_err_t erased=nvs_erase_key(n,"token");
    bool saved=(erased==ESP_OK || erased==ESP_ERR_NVS_NOT_FOUND) &&
        nvs_set_u8(n,"intro_skipped",0)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);
    if (saved) {
        v_status.connection_request=(vibe_connection_request_t){0};
        v_status.token[0]=0;v_status.audio_ready=false;v_status.hotkey_ready=false;
        v_status.peer_seen_ms=0;v_status.peer=false;
        intro_skipped=false;vibe_model_connection_boot(&v_model,false,false);
    }
    return saved;
}
/* Runs only on the controller task. Authentication precedes every mutation. */
static void receive_json(const char *s) {
    cJSON *o=cJSON_Parse(s); if (!o) return;
    const cJSON *t=cJSON_GetObjectItemCaseSensitive(o,"type");
    const cJSON *idj=cJSON_GetObjectItemCaseSensitive(o,"id");
    const char *id=cJSON_IsString(idj) && strlen(idj->valuestring)<=64 ? idj->valuestring : "";
    if (!cJSON_IsString(t)) { cJSON_Delete(o); return; }
    const cJSON *auth=cJSON_GetObjectItemCaseSensitive(o,"token");
    const char *token=cJSON_IsString(auth) ? auth->valuestring : "";
    if (!strcmp(t->valuestring,"pair")) {
        const cJSON *clientj=cJSON_GetObjectItemCaseSensitive(o,"client"),*requestj=cJSON_GetObjectItemCaseSensitive(o,"request");
        const char *client=cJSON_IsString(clientj)?clientj->valuestring:NULL;
        const char *request=cJSON_IsString(requestj)?requestj->valuestring:NULL;
        bool same=token_valid(token) && v_status.token[0] && !strcmp(token,v_status.token);
        if (vibe_connection_approved(token,client,request) || same) {
            nvs_handle_t n;
            bool saved=same || nvs_open("vibe",NVS_READWRITE,&n)==ESP_OK;
            if (saved && !same) { saved=nvs_set_str(n,"token",token)==ESP_OK && nvs_commit(n)==ESP_OK;nvs_close(n); }
            if (saved) {
                if (!same) { strcpy(v_status.token,token);vibe_network_commit(client);v_model.connection_from_menu=false; }
                v_status.connection_request.phase=V_CONNECT_NONE;prefs_reply(id,NULL);
            } else prefs_reply(id,"设备保存失败，请重试");
        }
    } else if (!v_status.token[0] || strcmp(token,v_status.token)) {
        /* Do not expose configuration to unauthenticated LAN traffic. */
    } else if (!strcmp(t->valuestring,"pc.unpair")) {
        bool saved=forget_computer();prefs_reply(id,saved?NULL:"设备保存失败，请重试");
        if (saved) vibe_network_release();
    } else if (!strcmp(t->valuestring,"pc.disconnect")) {
        v_status.peer_seen_ms=0;v_status.peer=false;
        v_status.audio_ready=false;v_status.hotkey_ready=false;vibe_network_release();
    } else if (!strcmp(t->valuestring,"pc.status")) {
        unsigned seconds=5;
        if (integer(o,"idleHeartbeat",&seconds,5,30)) v_status.heartbeat_ms=vibe_peer_interval(seconds);
        v_status.audio_ready=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"audioReady"));
        v_status.hotkey_ready=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"hotkeyReady"));
        bool had_radio_pcm=v_status.radio_pcm;
        unsigned radio_formats=0;
        v_status.radio_pcm=integer(o,"radioFormats",&radio_formats,0,255) && (radio_formats&(1u<<(V_RADIO_FORMAT-1)));
        if (!had_radio_pcm && v_status.radio_pcm && v_model.radio_active && v_model.radio_requested && !v_model.recording) vibe_audio_request(V_RADIO_CHANGED);
        const cJSON *ep=cJSON_GetObjectItemCaseSensitive(o,"epoch");
        if (cJSON_IsNumber(ep) && ep->valuedouble>=1700000000 && ep->valuedouble<=4102444800) {
            struct timeval tv={.tv_sec=(time_t)ep->valuedouble}; settimeofday(&tv,NULL);
        }
        const cJSON *tz=cJSON_GetObjectItemCaseSensitive(o,"tz");
        if (cJSON_IsNumber(tz) && isfinite(tz->valuedouble) && tz->valuedouble>=-720 && tz->valuedouble<=840 && floor(tz->valuedouble)==tz->valuedouble) {
            v_status.tz_minutes=tz->valueint;
            if (v_status.appearance.tz_minutes!=tz->valueint) {
                int previous=v_status.appearance.tz_minutes;
                v_status.appearance.tz_minutes=tz->valueint;
                if (!vibe_save_prefs()) v_status.appearance.tz_minutes=previous;
            }
        }
    } else if (!strcmp(t->valuestring,"weather.cache")) {
        const cJSON *latitude=cJSON_GetObjectItemCaseSensitive(o,"latitude"),*longitude=cJSON_GetObjectItemCaseSensitive(o,"longitude"),*city=cJSON_GetObjectItemCaseSensitive(o,"city");
        bool location=cJSON_IsNumber(latitude) && cJSON_IsNumber(longitude) && cJSON_IsString(city) &&
            latitude->valuedouble==v_prefs.latitude && longitude->valuedouble==v_prefs.longitude && !strcmp(city->valuestring,v_prefs.city);
        bool idle=v_prefs.weather_enabled && !v_model.recording && !v_model.radio_requested && !v_status.audio_busy && !v_status.provisioning;
        vibe_weather_t weather;vibe_air_t air;bool ok=location && idle && vibe_weather_cache_parse(cJSON_GetObjectItemCaseSensitive(o,"sample"),&weather,&air);
        time_t clock=time(NULL);
        if(ok && clock>=1700000000 && (weather.sampled_at<(int64_t)clock-3600 || weather.sampled_at>(int64_t)clock+300)) ok=false;
        if(ok) {
            v_status.forecast=weather;v_status.weather_at_ms=vibe_now();v_status.weather_attempt_ms=vibe_now();
            if(air.valid){v_status.air=air;v_status.air_at_ms=vibe_now();v_status.air_attempt_ms=vibe_now();}
        }
        cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"event","weather.reply");cJSON_AddStringToObject(r,"id",id);cJSON_AddBoolToObject(r,"ok",ok);send_object(r);
    } else if (!strcmp(t->valuestring,"power.get") || !strcmp(t->valuestring,"power.reset")) {
        send_object(vibe_power_reply(id,!strcmp(t->valuestring,"power.reset")));
    } else if (!strcmp(t->valuestring,"agent.notify")) {
        static const char *agents[]={"codex","claude","opencode","dsh","zcode","gemini","qwen"};
        static const char *kinds[]={"completed","approval","input","failed"};
        const cJSON *agent=cJSON_GetObjectItemCaseSensitive(o,"agent"),*kind=cJSON_GetObjectItemCaseSensitive(o,"kind");
        unsigned a=7,k=4,ttl=0;
        if (cJSON_IsString(agent)) for (unsigned i=0;i<7;i++) if (!strcmp(agent->valuestring,agents[i])) a=i;
        if (cJSON_IsString(kind)) for (unsigned i=0;i<4;i++) if (!strcmp(kind->valuestring,kinds[i])) k=i;
        bool valid=id[0] && strlen(id)==36 && integer(o,"ttl_ms",&ttl,1,10000);
        const cJSON *expires=cJSON_GetObjectItemCaseSensitive(o,"expires");
        const time_t epoch=time(NULL);
        if (!cJSON_IsNumber(expires) || !isfinite(expires->valuedouble)) valid=false;
        else if (epoch>=1700000000 && expires->valuedouble<(double)epoch) valid=false;
        bool accepted=valid && vibe_agent_push(&v_status.agent_alert,vibe_agent_hash(id),a,k,vibe_now(),ttl);
        cJSON *reply=cJSON_CreateObject();cJSON_AddStringToObject(reply,"event","agent.reply");cJSON_AddStringToObject(reply,"id",id);
        cJSON_AddBoolToObject(reply,"ok",accepted);send_object(reply);
    } else if (!strcmp(t->valuestring,"playback.status")) {
        const cJSON *session=cJSON_GetObjectItemCaseSensitive(o,"session");
        if (cJSON_IsString(session)) vibe_radio_remote_status(session->valuestring,cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"failed")));
    } else if (!strncmp(t->valuestring,"radio.",6)) {
        if ((v_model.recording || v_status.audio_busy || v_model.radio_requested) && strcmp(t->valuestring,"radio.get") && strcmp(t->valuestring,"radio.abort")) {
            cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"event","radio.reply");cJSON_AddStringToObject(r,"id",id);
            cJSON_AddBoolToObject(r,"ok",false);cJSON_AddStringToObject(r,"error","请先结束语音或收听再保存电台");send_object(r);
        } else send_object(vibe_radio_library_request(o));
    } else if (!strcmp(t->valuestring,"config.get")) prefs_reply(id,NULL);
    else if (!strcmp(t->valuestring,"config.set")) {
        vibe_prefs_t p=v_prefs;vibe_appearance_t a=v_status.appearance;
        if (!prefs_parse(cJSON_GetObjectItemCaseSensitive(o,"settings"),&p,&a)) prefs_reply(id,"配置格式或范围不正确");
        else if ((v_status.audio_busy || v_model.recording) && !vibe_prefs_volume_only(&v_prefs,&p,&v_status.appearance,&a))
            prefs_reply(id,"播放期间可调音量，其他设置请停止音频后修改");
        else {
            if (memcmp(&p,&v_prefs,sizeof(p)) || memcmp(&a,&v_status.appearance,sizeof(a))) {
                vibe_prefs_t previous=v_prefs;vibe_appearance_t previous_a=v_status.appearance;
                v_prefs=p;v_status.appearance=a;
                if (!vibe_save_prefs()) { v_prefs=previous;v_status.appearance=previous_a; prefs_reply(id,"设备保存失败，请重试"); cJSON_Delete(o); return; }
                model_from_prefs();
                if (p.latitude!=previous.latitude || p.longitude!=previous.longitude || p.weather_enabled!=previous.weather_enabled || strcmp(p.city,previous.city)) {
                    v_status.weather_at_ms=0; v_status.weather_attempt_ms=0; v_status.forecast.valid=false;
                    v_status.air_at_ms=0; v_status.air_attempt_ms=0; v_status.air.valid=false;
                }
                snprintf(v_status.notice,sizeof(v_status.notice),"设置已更新");
            }
            prefs_reply(id,NULL);
        }
    }
    cJSON_Delete(o);
}
static void key_callback(bsp_btn_t key,bsp_btn_ev_t event,void *user) {
    (void)user;
    if (key_guard_consume(&wake_guard,event==BSP_BTN_RELEASE,esp_timer_get_time())) return;
    if (event!=BSP_BTN_PRESS && event!=BSP_BTN_RELEASE && event!=BSP_BTN_LONG) return;
    vibe_msg_t msg={.type=V_MSG_KEY,.key=key,.event=(int)event|
        (atomic_load_explicit(&screen_visible,memory_order_acquire)?V_KEY_SCREEN_VISIBLE:0),
        .ui_frame=vibe_ui_connection_frame(NULL)};
    if (xQueueSend(v_queue,&msg,0)!=pdTRUE) input_overflow=true;
}
static void execute(unsigned actions) {
    vibe_radio_catalog_sync();
    if ((actions&V_RADIO_CHANGED) && !v_model.radio_active) vibe_radio_remember();
    if (actions & (V_START_AUDIO|V_STOP_AUDIO|V_ALARM|V_RADIO_CHANGED|V_RADIO_STATION|V_AGENT_ALERT)) {
        if (actions & (V_START_AUDIO|V_ALARM|V_RADIO_CHANGED|V_RADIO_STATION)) vibe_radio_cancel_transfer();
        if (!vibe_audio_request(actions)) {
            if (actions&V_ALARM) v_model.alarm_pending=true;
            if (actions&V_START_AUDIO) { v_model.recording=false; v_status.audio_fault=true; }
        }
    }
    if (actions&V_ENTER) vibe_send_json("{\"event\":\"key.action\",\"action\":\"enter\"}");
    if (actions&V_PREFS_CHANGED) {
        unsigned brightness=v_prefs.brightness,volume=v_prefs.volume,seconds=v_prefs.screen_seconds;
        uint32_t theme=v_status.appearance.mode;
        v_prefs.brightness=v_model.brightness; v_prefs.volume=v_model.volume;
        v_prefs.screen_seconds=v_model.screen_seconds;
        v_status.appearance.mode=v_model.theme_mode;
        if (vibe_save_prefs()) prefs_message("config.changed",NULL,NULL);
        else {
            v_prefs.brightness=brightness;v_prefs.volume=volume;v_prefs.screen_seconds=seconds;
            v_status.appearance.mode=theme;model_from_prefs();
        }
    }
    if ((actions&V_START_PROVISION) && vibe_provision_start()) {
        v_model.radio_requested=false;vibe_audio_request(V_RADIO_CHANGED);vibe_model_provision_open(&v_model,vibe_now());
    }
    if (actions&V_FINISH_PROVISION) vibe_provision_finish();
    if (actions&V_DISMISS_INTRO) {
        nvs_handle_t n;bool saved=nvs_open("vibe",NVS_READWRITE,&n)==ESP_OK;
        if (saved) { saved=nvs_set_u8(n,"intro_skipped",1)==ESP_OK && nvs_commit(n)==ESP_OK;nvs_close(n); }
        if (!saved) ESP_LOGW(TAG,"Cannot save onboarding dismissal");
        intro_skipped=saved;vibe_model_intro_saved(&v_model,saved,vibe_now());
    }
    if (actions&V_UNPAIR) {
        if (forget_computer()) vibe_network_release();
    }
}
static void report_memory(void) {
    static const char *const names[]={"main","vibe_audio","vibe_net","vibe_weather","taskLVGL","nimble_host"};
    ESP_LOGI(TAG,"Memory free=%u largest=%u minimum=%u",(unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)esp_get_minimum_free_heap_size());
    for (unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        /* Weather is disposable. Hold its lifecycle lock across handle use. */
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        TaskHandle_t task=xTaskGetHandle(names[i]);
        if (task) ESP_LOGI(TAG,"Stack %s unused=%u",names[i],(unsigned)uxTaskGetStackHighWaterMark(task));
        xSemaphoreGive(v_mutex);
    }
}
void app_main(void) {
    v_mutex=xSemaphoreCreateMutex(); v_queue=xQueueCreate(12,sizeof(vibe_msg_t));
    if (!v_mutex || !v_queue) { ESP_LOGE(TAG,"No controller memory"); return; }
    esp_err_t n=nvs_flash_init();
    if (n!=ESP_OK) { ESP_LOGE(TAG,"NVS unavailable (%s); retained data was not erased",esp_err_to_name(n)); return; }
    load_prefs(); vibe_model_init(&v_model,vibe_now()); model_from_prefs();
    vibe_model_connection_boot(&v_model,v_status.token[0]!=0,intro_skipped);
    vibe_radio_catalog_init();
    v_status.tz_minutes=v_status.appearance.tz_minutes; v_status.battery=-1;v_status.heartbeat_ms=5000;
    uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(v_status.device_id,sizeof(v_status.device_id),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    ESP_ERROR_CHECK(bsp_i2c_init()); ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) return;
    if (bsp_battery_init()==ESP_OK) v_status.battery=bsp_battery_soc();
    ESP_ERROR_CHECK(vibe_audio_init());
    ESP_ERROR_CHECK(vibe_network_init());
    ESP_LOGI(TAG,"BLE: %s",esp_err_to_name(vibe_ble_init()));
    vibe_ui_init();
    if (esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO)
        key_guard_arm(&wake_guard,esp_timer_get_time(),10000000);
    ESP_ERROR_CHECK(bsp_button_init(key_callback,NULL));
    key_guard_sync_released(&wake_guard,bsp_button_read_mv()>=BSP_BTN_MV_RELEASED_MIN);
    // LEDC's default mode does not keep PWM alive in Light-sleep. Only allow
    // automatic sleep after blanking the backlight and suspending the panel.
    esp_pm_lock_handle_t screen_pm_lock;
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"vibe_screen",&screen_pm_lock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(screen_pm_lock));
    bool screen_awake=true;
    esp_pm_config_t pm={.max_freq_mhz=160,.min_freq_mhz=40,.light_sleep_enable=true};
    ESP_LOGI(TAG,"Power management: %s",esp_err_to_name(esp_pm_configure(&pm)));
    ESP_LOGI(TAG,"Power diagnostics: %s",esp_err_to_name(vibe_power_init()));
    uint64_t painted=0,battery_at=vibe_now(),profile_at=15000,readiness_ping_at=0; int last_bl=-1;
    for (;;) {
        vibe_msg_t msg;
        bool have=xQueueReceive(v_queue,&msg,pdMS_TO_TICKS(vibe_model_wait_ms(&v_model,vibe_now())))==pdTRUE;
        bool key_screen_visible=have && msg.type==V_MSG_KEY && (msg.event&V_KEY_SCREEN_VISIBLE);
        if (have && msg.type==V_MSG_KEY) msg.event&=~V_KEY_SCREEN_VISIBLE;
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        uint64_t now=vibe_now();
        bool had_radio_peer=v_status.peer && v_status.radio_pcm;
        v_status.peer=v_status.peer_seen_ms && now-v_status.peer_seen_ms<6000 && (v_status.wifi || v_status.ble_peer);
        if (!had_radio_peer && v_status.peer && v_status.radio_pcm && v_model.radio_active && v_model.radio_requested && !v_model.recording) vibe_audio_request(V_RADIO_CHANGED);
        v_model.enter_ready=v_status.peer && v_status.hotkey_ready && v_status.token[0];
        /* A prior codec failure must not disable Enter or prevent a wake retry. */
        v_model.voice_ready=v_model.enter_ready && v_status.audio_ready;
        if (input_overflow) {
            memset(&prompt_input,0,sizeof(prompt_input));prompt_input.consumed=7;
        }
        if (input_overflow || (v_model.recording && !v_model.voice_ready)) {
            input_overflow=false; v_model.recording=false; execute(V_STOP_AUDIO);
            memset(held,0,sizeof(held));
        }
        if (have && msg.type==V_MSG_JSON) { if (vibe_network_generation_matches(msg.link_generation)) receive_json(msg.json);vibe_message_release(&msg); }
        if (have && msg.type==V_MSG_PROVISION) {
            if (msg.event==1) vibe_model_provision_open(&v_model,now);
            else vibe_model_provision_closed(&v_model,msg.event==2,v_status.peer,now);
        }
        vibe_model_connection_complete(&v_model,v_status.peer && v_status.token[0],now);
        static bool requested_setup_close;
        bool finish_guide=v_model.page==V_PROVISION && vibe_model_connection_page(v_model.provision_return) &&
            !v_model.connection_from_menu && v_status.peer && v_status.token[0];
        if (finish_guide && !requested_setup_close) requested_setup_close=vibe_provision_connected();
        if (!finish_guide) requested_setup_close=false;
        if (have && msg.type==V_MSG_AUDIO_FAIL) { v_model.recording=false; v_status.audio_fault=true; }
        vibe_connect_tick(&v_status.connection_request,now);
        bool connection_prompt=v_status.connection_request.phase==V_CONNECT_PENDING;
        uint32_t prompt_frame=vibe_ui_connection_frame(v_status.connection_request.request);
        vibe_prompt_input_sync(&prompt_input,connection_prompt,v_status.connection_request.request,prompt_frame);
        static uint32_t reported_prompt_frame;
        if (!connection_prompt) reported_prompt_frame=0;
        else if (prompt_frame && prompt_frame!=reported_prompt_frame && atomic_load_explicit(&screen_visible,memory_order_acquire)) {
            reported_prompt_frame=prompt_frame;
            ESP_LOGI(TAG,"Connection confirmation visible frame=%lu",(unsigned long)prompt_frame);
        }
        if (have && msg.type==V_MSG_KEY && msg.key>=0 && msg.key<3) {
            int k=msg.key;
            vibe_event_t event=msg.event==BSP_BTN_PRESS?V_PRESS:msg.event==BSP_BTN_RELEASE?V_RELEASE:V_LONG;
            vibe_input_result_t result=vibe_prompt_input_event(&prompt_input,k,event,key_screen_visible,msg.ui_frame);
            if (result!=V_INPUT_PASS) {
                if (result==V_INPUT_ACCEPT || result==V_INPUT_REJECT)
                    vibe_connect_decide(&v_status.connection_request,result==V_INPUT_ACCEPT,now);
                held[k]=false;long_press[k]=false;msg.type=V_MSG_CONNECTION_CHANGED;
            }
        }
        if (connection_prompt) { v_model.screen_on=true;v_model.last_activity_ms=now; }
        if (have && msg.type==V_MSG_KEY && msg.key>=0 && msg.key<3) {
            const int k=msg.key;
            /* Dismiss first, then deliver this exact gesture to the application.
             * Never consume a press or synthesize a second click/release. */
            if (msg.event==BSP_BTN_PRESS && key_screen_visible) dismiss_agent_notice();
            const vibe_page_t before_page=v_model.page;
            const vibe_timer_t before_timer=v_model.timer;
            unsigned a=0;
            if (msg.event==BSP_BTN_PRESS) {
                if (!key_screen_visible) v_model.screen_on=false;
                held[k]=true;long_press[k]=false;a=vibe_model_key(&v_model,k,V_PRESS,now);
            }
            else if (msg.event==BSP_BTN_LONG && held[k]) { long_press[k]=true; a=vibe_model_key(&v_model,k,V_LONG,now); }
            else if (msg.event==BSP_BTN_RELEASE && held[k]) {
                held[k]=false; a=vibe_model_key(&v_model,k,V_RELEASE,now);
                if (!long_press[k]) a|=vibe_model_key(&v_model,k,V_CLICK,now);
            }
            if (a&(V_START_AUDIO|V_ENTER)) dismiss_agent_notice();
            execute(a);
            /* Bounded per-gesture diagnostics: no audio, network or credentials. */
            ESP_LOGI(TAG,"key=%d event=%d page=%d->%d timer=%d->%d left=%llu",k,msg.event,
                before_page,v_model.page,before_timer,v_model.timer,
                (unsigned long long)vibe_model_remaining(&v_model,now));
        }
        if (vibe_agent_tick(&v_status.agent_alert,now,v_model.recording || v_model.page==V_PROVISION || connection_prompt))
            execute(V_AGENT_ALERT);
        execute(vibe_model_tick(&v_model,now));
        if (v_status.agent_alert.visible) { v_model.screen_on=true;v_model.last_activity_ms=now; }
        int bl=v_model.screen_on ? v_model.brightness : 0;
        bool voice_ready=v_model.voice_ready;
        xSemaphoreGive(v_mutex);
        // Refresh for the first visible frame after standby, then once a minute
        // while visible. No fuel-gauge reads or extra timer while blanked.
        if (bl && (!screen_awake || now-battery_at>=60000)) {
            int soc=bsp_battery_soc();
            xSemaphoreTake(v_mutex,portMAX_DELAY); v_status.battery=soc; xSemaphoreGive(v_mutex); battery_at=now;
        }
        if (bl!=last_bl || (bl && !atomic_load_explicit(&screen_visible,memory_order_acquire))) {
            bool screen_just_woke=bl && last_bl==0;
            if (!bl) { atomic_store_explicit(&screen_visible,false,memory_order_release);bsp_display_backlight(0); }
            if (bl && !screen_awake) {
                ESP_ERROR_CHECK(esp_pm_lock_acquire(screen_pm_lock));
                screen_awake=true;
            }
            // Process/enqueue voice and notification actions before the panel's
            // blocking wake. Neither the model nor its mutex sleeps with LVGL.
            if (bsp_lvgl_set_sleep(!bl)==ESP_OK) {
                if (bl) { vibe_ui_refresh();bsp_display_backlight(bl);atomic_store_explicit(&screen_visible,true,memory_order_release); }
                else if (screen_awake) {
                    ESP_ERROR_CHECK(esp_pm_lock_release(screen_pm_lock));
                    screen_awake=false;
                }
                last_bl=bl;
                vibe_power_screen(bl!=0);
                vibe_network_wake();
                if (screen_just_woke && !voice_ready &&
                    (!readiness_ping_at || now-readiness_ping_at>=2000)) {
                    readiness_ping_at=now;
                    vibe_send_json("{\"event\":\"pc.ping\"}");
                }
                ESP_LOGI(TAG,"Screen %s",bl?"awake":"asleep");
            }
        }
        if ((have && msg.type==V_MSG_KEY) || now-painted>=200) { vibe_ui_refresh(); painted=now; }
        if (now>=profile_at) {
            if (usb_serial_jtag_is_connected()) report_memory();
            profile_at=now+60000;
        }
    }
}
