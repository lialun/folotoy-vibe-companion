#include "../../main/vibe/diagnostics.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned wakes,sends;
static uint64_t now;
static vibe_route_t route;
static esp_app_desc_t app={.version="0.8.3-test"};
uint64_t vibe_now(void) { return now; }
size_t heap_caps_get_free_size(uint32_t caps) { assert(caps==MALLOC_CAP_8BIT);return 40000; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { assert(caps==MALLOC_CAP_8BIT);return 20000; }
const esp_app_desc_t *esp_app_get_description(void) { return &app; }
vibe_route_t vibe_transport_route(void) { return route; }
void vibe_network_wake(void) { wakes++; }
void vibe_send_json(const char *line) { assert(strstr(line,"diagnostics.available") && strlen(line)<80);sends++; }
int main(int argc,char **argv) {
    assert(sizeof(ring)==200);vibe_diagnostics_flush();assert(!wakes && !sends);
    for (unsigned i=0;i<20;i++) { now=i*1000;vibe_diagnostics_record(V_DIAG_RADIO_TIMEOUT,(int32_t)i); }
    assert(wakes==20 && ring.count==8 && vibe_diagnostics_sequence()==20);
    vibe_diagnostics_flush();assert(!sends); /* disconnected: no busy sends */
    route.kind=V_LINK_BLE;vibe_diagnostics_flush();vibe_diagnostics_flush();assert(sends==1);
    vibe_diagnostic_t rows[8];assert(vibe_diagnostic_read(&ring,0,rows,8)==8 && rows[0].sequence==13 && rows[7].sequence==20);
    assert(vibe_diagnostic_read(&ring,20,rows,8)==0);
    assert(vibe_diagnostic_read(&ring,14,rows,3)==3 && rows[0].sequence==15);
    cJSON *reply=vibe_diagnostics_reply("test",0);char *encoded=cJSON_PrintUnformatted(reply);assert(encoded && strlen(encoded)<1200);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(reply,"records"))==3);
    assert(cJSON_GetObjectItem(reply,"oldest")->valueint==13);
    assert(strlen(cJSON_GetObjectItem(reply,"elf")->valuestring)==64);
    if (argc>1) { FILE *file=fopen(argv[1],"w");assert(file);fputs(encoded,file);fclose(file); }
    free(encoded);cJSON_Delete(reply);
    ring.sequence=UINT32_MAX;vibe_diagnostics_record(V_DIAG_BOOT,1);assert(ring.count==1 && ring.sequence==1);
    assert(vibe_diagnostic_read(&ring,0,rows,8)==1 && rows[0].code==V_DIAG_BOOT);
    puts("Device diagnostics: 200-byte ring, offline overwrite, paging, bounded JSON, wake-on-event and wrap PASS");
}
