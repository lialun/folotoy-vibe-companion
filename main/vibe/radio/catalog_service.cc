#include "catalog_service.h"
#include "library.h"
#include "radio_presets.h"
#include "vibe.h"
#include "nvs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
vibe_library_t library;
uint64_t verified;
char last_station[257];
bool persist(const vibe_station_list_t *list,void *) {
    size_t size=vibe_station_list_storage_bytes(list);
    void *blob=size?std::malloc(size):nullptr;
    if (!blob) return false;
    if (!vibe_station_list_encode(list,blob,size)) { std::free(blob);return false; }
    nvs_handle_t n;
    if (nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) { std::free(blob);return false; }
    bool ok=nvs_set_blob(n,"radio_list",blob,size)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);std::free(blob);return ok;
}
}
extern "C" void vibe_radio_catalog_init(void) {
    // Keep preferences/Wi-Fi/pairing schemas intact. Radio has its own atomic blob.
    nvs_handle_t n; bool loaded=false;
    if (nvs_open("vibe",NVS_READONLY,&n)==ESP_OK) {
        size_t size=0;
        if (nvs_get_blob(n,"radio_list",nullptr,&size)==ESP_OK && size>=sizeof(vibe_station_list_t) && size<=vibe_station_list_bytes(VIBE_STATION_MAX)) {
            void *blob=std::malloc(size);
            if (blob) {
                if (nvs_get_blob(n,"radio_list",blob,&size)==ESP_OK) library.active=vibe_station_list_decode(blob,size);
                std::free(blob);loaded=library.active!=nullptr;
            }
        }
        size_t saved_size=sizeof(last_station);nvs_get_str(n,"radio_last",last_station,&saved_size);
        nvs_close(n);
    }
    if (!loaded) {
        constexpr unsigned defaults=sizeof(kRadioPresets)/sizeof(kRadioPresets[0]);
        static_assert(defaults<=VIBE_STATION_MAX,"Default stations exceed list capacity");
        bool duplicate=false;
        for (const auto &preset:kRadioPresets) duplicate|=!std::strcmp(preset.url,v_prefs.radio_url);
        vibe_station_t legacy{};std::snprintf(legacy.name,sizeof(legacy.name),"%s",v_prefs.radio_name);std::snprintf(legacy.url,sizeof(legacy.url),"%s",v_prefs.radio_url);
        const bool keep_legacy=defaults<VIBE_STATION_MAX && !duplicate && vibe_station_valid(&legacy);
        // Reserve the legacy slot only when it will contain a distinct station.
        library.active=vibe_station_list_create(defaults+(keep_legacy?1:0));
        if (!library.active) { v_model.radio_count=0;return; }
        library.active->count=0;
        for (const auto &preset:kRadioPresets) {
            auto &station=library.active->stations[library.active->count++];
            std::snprintf(station.name,sizeof(station.name),"%.48s",preset.name);
            std::snprintf(station.url,sizeof(station.url),"%s",preset.url);
        }
        if (keep_legacy) library.active->stations[library.active->count++]=legacy;
    }
    for (unsigned i=0;i<library.active->count;i++) if (!std::strcmp(last_station,library.active->stations[i].url)) v_model.radio_index=i;
    vibe_radio_catalog_sync();
}
extern "C" void vibe_radio_catalog_sync(void) {
    vibe_library_expire(&library,vibe_now());
    v_model.radio_count=library.active?library.active->count:0;
    if (v_model.radio_index>=v_model.radio_count) v_model.radio_index=0;
    static const vibe_station_t empty{};
    const auto &station=v_model.radio_count?library.active->stations[v_model.radio_index]:empty;
    if (!v_model.radio_count) v_model.radio_requested=false;
    std::snprintf(v_status.radio_name,sizeof(v_status.radio_name),"%s",station.name);
    std::snprintf(v_status.radio_description,sizeof(v_status.radio_description),"%s",v_model.radio_count?library.active->stations[(v_model.radio_index+1)%library.active->count].name:"");
}
extern "C" void vibe_radio_catalog_request(void) { /* Directory browsing belongs to the Mac. */ }
extern "C" void vibe_radio_cancel_transfer(void) { vibe_library_abort(&library); }
extern "C" cJSON *vibe_radio_library_request(const cJSON *request) {
    uint32_t revision=library.active?library.active->revision:0;
    char previous[257];std::snprintf(previous,sizeof(previous),"%s",library.active && library.active->count?library.active->stations[v_model.radio_index].url:"");
    uint64_t next_verified=0;
    if (library.staged && library.active) for (unsigned i=0;i<library.staged->count;i++) for (unsigned j=0;j<library.active->count;j++)
        if ((verified&(UINT64_C(1)<<j)) && !std::strcmp(library.staged->stations[i].url,library.active->stations[j].url)) next_verified|=UINT64_C(1)<<i;
    cJSON *reply=vibe_library_handle(&library,request,vibe_now(),persist,nullptr);
    if (library.active && library.active->revision!=revision) {
        verified=next_verified;v_model.radio_index=0;bool found=false;
        for (unsigned i=0;i<library.active->count;i++) if (!std::strcmp(previous,library.active->stations[i].url)) { v_model.radio_index=i;found=true;break; }
        if (!found) { v_model.radio_requested=false;vibe_audio_request(V_RADIO_CHANGED); }
    }
    const cJSON *index=cJSON_GetObjectItemCaseSensitive(request,"index");
    if (cJSON_IsNumber(index) && index->valueint>=0 && index->valueint<VIBE_STATION_MAX)
        cJSON_AddBoolToObject(reply,"verified",(verified&(UINT64_C(1)<<index->valueint))!=0);
    vibe_radio_catalog_sync();return reply;
}
extern "C" void vibe_radio_url(char *url,unsigned size) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    std::snprintf(url,size,"%s",library.active && library.active->count?library.active->stations[v_model.radio_index<library.active->count?v_model.radio_index:0].url:"");
    xSemaphoreGive(v_mutex);
}
extern "C" void vibe_radio_status(unsigned state,const char *detail) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.radio_state=state;
    v_status.radio_playing=state==V_RADIO_PLAYING;
    if (state==V_RADIO_ERROR) std::snprintf(v_status.radio_error,sizeof(v_status.radio_error),"%s",detail?detail:"");
    else if (state==V_RADIO_CONNECTING) v_status.radio_error[0]=0;
    if (detail) std::snprintf(v_status.notice,sizeof(v_status.notice),"%s",detail);
    xSemaphoreGive(v_mutex);
}

extern "C" void vibe_radio_remember(void) {
    if (!library.active || !library.active->count) return;
    const char *url=library.active->stations[v_model.radio_index].url;
    if (!url[0] || !std::strcmp(url,last_station)) return;
    nvs_handle_t n;if (nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) return;
    if (nvs_set_str(n,"radio_last",url)==ESP_OK && nvs_commit(n)==ESP_OK) std::snprintf(last_station,sizeof(last_station),"%s",url);
    nvs_close(n);
}

extern "C" void vibe_radio_played(const char *url) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    for (unsigned i=0;library.active && i<library.active->count;i++) if (!std::strcmp(url,library.active->stations[i].url) && !(verified&(UINT64_C(1)<<i))) {
        verified|=UINT64_C(1)<<i;
        cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"event","radio.verified");cJSON_AddStringToObject(o,"url",url);
        char *s=cJSON_PrintUnformatted(o);if (s) { vibe_send_json(s);free(s); }cJSON_Delete(o);break;
    }
    xSemaphoreGive(v_mutex);
}
