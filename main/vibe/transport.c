#include "vibe.h"
#include "transport.h"
#include "ble.h"
#include "radio/relay.h"
#include "cJSON.h"
#include "esp_random.h"
#include <stdlib.h>
#include <string.h>
typedef struct { vibe_route_t route;uint32_t lease;char json[]; } outgoing_t;
static SemaphoreHandle_t lock;
static QueueHandle_t outgoing;
static vibe_route_t selected;
static uint32_t current_lease,sequence,boot;
static vibe_route_t voice_route;
static uint32_t voice_lease;
esp_err_t vibe_transport_init(void) {
    boot=esp_random();lock=xSemaphoreCreateMutex();outgoing=xQueueCreate(8,sizeof(outgoing_t *));
    return lock && outgoing?ESP_OK:ESP_ERR_NO_MEM;
}
uint32_t vibe_transport_boot(void) { return boot; }
vibe_route_t vibe_transport_route(void) {
    xSemaphoreTake(lock,portMAX_DELAY);vibe_route_t route=selected;xSemaphoreGive(lock);return route;
}
void vibe_transport_owner(vibe_route_t route,uint32_t lease) {
    vibe_relay_abort();
    xSemaphoreTake(lock,portMAX_DELAY);selected=route;current_lease=lease;xSemaphoreGive(lock);
}
static void enqueue(const char *s,vibe_route_t route,uint32_t lease) {
    size_t n=strlen(s);if (!outgoing || !route.kind || n>VIBE_JSON_MAX) return;
    outgoing_t *p=malloc(sizeof(*p)+n+1);if (!p) return;
    p->route=route;p->lease=lease;memcpy(p->json,s,n+1);
    if (xQueueSend(outgoing,&p,0)!=pdTRUE) free(p);else vibe_network_wake();
}
void vibe_transport_reply(const char *s,vibe_route_t route) { enqueue(s,route,0); }
void vibe_send_json(const char *s) {
    xSemaphoreTake(lock,portMAX_DELAY);vibe_route_t route=selected;uint32_t lease=current_lease;xSemaphoreGive(lock);
    enqueue(s,route,lease);
}
static int packet(vibe_route_t route,unsigned char type,const void *bytes,size_t size) {
    return route.kind==V_LINK_WIFI?vibe_wifi_send(route,type,bytes,size):
        route.kind==V_LINK_BLE?vibe_ble_send(route,type,bytes,size):-1;
}
int vibe_send_audio(const void *bytes,size_t size) {
    xSemaphoreTake(lock,portMAX_DELAY);
    int result=voice_lease==current_lease && vibe_route_equal(voice_route,selected)?packet(voice_route,1,bytes,size):-1;
    xSemaphoreGive(lock);return result;
}
void vibe_transport_reserve_voice(void) {
    xSemaphoreTake(lock,portMAX_DELAY);voice_route=selected;voice_lease=current_lease;xSemaphoreGive(lock);
}
bool vibe_transport_voice_begin(void) {
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=voice_lease==current_lease && vibe_route_equal(voice_route,selected);
    if (ok) enqueue("{\"event\":\"voice.start\",\"audio\":\"ima_adpcm\"}",voice_route,voice_lease);
    xSemaphoreGive(lock);return ok;
}
void vibe_transport_voice_end(void) {
    xSemaphoreTake(lock,portMAX_DELAY);
    enqueue("{\"event\":\"voice.end\"}",voice_route,voice_lease);voice_route=(vibe_route_t){0};voice_lease=0;
    xSemaphoreGive(lock);
}
void vibe_transport_flush(void) {
    outgoing_t *p;
    while (xQueueReceive(outgoing,&p,0)==pdTRUE) {
        xSemaphoreTake(lock,portMAX_DELAY);
        bool stale=p->lease && (p->lease!=current_lease || !vibe_route_equal(p->route,selected));
        xSemaphoreGive(lock);
        if (!stale) {
            bool critical=strstr(p->json,"voice.start") || strstr(p->json,"voice.end") || strstr(p->json,"key.action");
            cJSON *o=cJSON_Parse(p->json);
            if (o) {
                if (!cJSON_GetObjectItemCaseSensitive(o,"boot")) cJSON_AddNumberToObject(o,"boot",boot);
                if (p->lease && !cJSON_GetObjectItemCaseSensitive(o,"lease")) cJSON_AddNumberToObject(o,"lease",p->lease);
                if (critical) cJSON_AddNumberToObject(o,"eid",++sequence);
                char *line=cJSON_PrintUnformatted(o);cJSON_Delete(o);
                if (line) {
                    for (int i=0;i<(critical && p->route.kind==V_LINK_WIFI?3:1);i++) packet(p->route,2,line,strlen(line));
                    free(line);
                }
            }
        }
        free(p);
    }
}
