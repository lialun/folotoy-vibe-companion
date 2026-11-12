#include "diagnostics.h"
#include "cJSON.h"
#include "vibe.h"
#include "transport.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>

/* No task, timer, Flash writes or formatted-message buffer. */
static vibe_diagnostic_ring_t ring;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t announced;
void vibe_diagnostics_record(unsigned code,int32_t detail) {
    vibe_diagnostic_t row={.uptime_ms=(uint32_t)vibe_now(),.code=(uint16_t)code,.detail=detail,
        .free=heap_caps_get_free_size(MALLOC_CAP_8BIT),.largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        .link=(uint8_t)vibe_transport_route().kind};
    portENTER_CRITICAL(&mux);vibe_diagnostic_push(&ring,row);portEXIT_CRITICAL(&mux);
    vibe_network_wake();
}
uint32_t vibe_diagnostics_sequence(void) {
    portENTER_CRITICAL(&mux);uint32_t seq=ring.sequence;portEXIT_CRITICAL(&mux);return seq;
}
void vibe_diagnostics_flush(void) {
    uint32_t seq=vibe_diagnostics_sequence();
    if (!seq || seq==announced || !vibe_transport_route().kind) return;
    announced=seq;
    char line[80];snprintf(line,sizeof(line),"{\"event\":\"diagnostics.available\",\"sequence\":%lu}",(unsigned long)seq);
    vibe_send_json(line);
}
cJSON *vibe_diagnostics_reply(const char *id,uint32_t after) {
    vibe_diagnostic_t rows[3];uint32_t newest,oldest;
    portENTER_CRITICAL(&mux);
    newest=ring.sequence;oldest=ring.count?newest-ring.count+1:0;
    unsigned count=vibe_diagnostic_read(&ring,after,rows,3);
    portEXIT_CRITICAL(&mux);
    const esp_app_desc_t *app=esp_app_get_description();char elf[65];
    for (unsigned i=0;i<32;i++) snprintf(elf+i*2,3,"%02x",app->app_elf_sha256[i]);
    cJSON *o=cJSON_CreateObject();if (!o) return NULL;
    cJSON_AddStringToObject(o,"event","diagnostics.reply");cJSON_AddStringToObject(o,"id",id);
    cJSON_AddNumberToObject(o,"sequence",newest);cJSON_AddNumberToObject(o,"oldest",oldest);
    cJSON_AddStringToObject(o,"firmware",app->version);cJSON_AddStringToObject(o,"elf",elf);
    cJSON *array=cJSON_AddArrayToObject(o,"records");
    for (unsigned i=0;i<count;i++) {
        const vibe_diagnostic_t *r=&rows[i];cJSON *item=cJSON_CreateObject();
        cJSON_AddNumberToObject(item,"sequence",r->sequence);cJSON_AddNumberToObject(item,"uptime_ms",r->uptime_ms);
        cJSON_AddNumberToObject(item,"code",r->code);cJSON_AddNumberToObject(item,"detail",r->detail);
        cJSON_AddNumberToObject(item,"free",r->free);cJSON_AddNumberToObject(item,"largest",r->largest);
        cJSON_AddNumberToObject(item,"link",r->link);cJSON_AddItemToArray(array,item);
    }
    return o;
}
