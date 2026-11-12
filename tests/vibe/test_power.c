#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../../main/vibe/power.c"

static int64_t clock_us=1000000;
static esp_pm_light_sleep_cb_t callback;
static bool usb;
static unsigned lock_rows=3;
static unsigned heap_reads;
void heap_caps_get_info(multi_heap_info_t *info,uint32_t caps) {
    assert(caps==MALLOC_CAP_8BIT);++heap_reads;
    *info=(multi_heap_info_t){.total_free_bytes=32000,.total_allocated_bytes=210000,
                            .largest_free_block=18000,.minimum_free_bytes=12000};
}
int64_t esp_timer_get_time(void) { return clock_us; }
void esp_sleep_set_sleep_context(esp_sleep_context_t *s) { assert(s==&sleep_context); }
esp_err_t esp_pm_light_sleep_register_cbs(esp_pm_sleep_cbs_register_config_t *c) {
    assert(!c->enter_cb && c->exit_cb);callback=c->exit_cb;return ESP_OK;
}
esp_err_t esp_pm_dump_locks(FILE *f) {
    fputs("Name Type Arg Active Total_count Time(us) Time(%)\n",f);
    for (unsigned i=0;i<lock_rows;++i) fprintf(f,"lock%u NO_LIGHT_SLEEP 0 %u 8 3500000 4%%\n",i,usb?1:0);
    fputs("Mode stats:\nSLEEP 40M 5000000 80%\n",f);
    return ESP_OK;
}
bool usb_serial_jtag_is_connected(void) { return usb; }
bool bsp_button_idle_wakeup_armed(void) { return true; }
int bsp_battery_soc(void) { return 84; }
int bsp_battery_mv(void) { return 4000; }
static double number(cJSON *o,const char *key) { cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);assert(cJSON_IsNumber(j));return j->valuedouble; }
int main(void) {
    assert(vibe_power_init()==ESP_OK);
    /* A PM exit with no SDK attempt must never look like successful sleep. */
    callback(0,NULL);assert(stats.sleeps==0 && stats.rejects==0);
    vibe_power_screen(false);
    for (unsigned i=0;i<60;++i) {
        ++sleep_context.lightsleep_cnt;sleep_context.sleep_request_result=ESP_OK;
        clock_us+=1000000;callback(900000,NULL);
    }
    ++sleep_context.lightsleep_cnt;sleep_context.sleep_request_result=ESP_FAIL;callback(50,NULL);
    callback(0,NULL); /* Rejected attempt must not be counted twice. */
    cJSON *o=vibe_power_reply("request",false);assert(o);
    assert(number(o,"sleeps")==60 && number(o,"rejects")==1);
    assert(number(o,"elapsed_ms")==60000 && number(o,"blank_ms")==60000);
    assert(number(o,"blank_sleep_ms")==54000 && number(o,"longest_ms")==900);
    cJSON *memory=cJSON_GetObjectItemCaseSensitive(o,"heap");
    assert(number(memory,"free")==32000 && number(memory,"used")==210000);
    assert(number(memory,"largest")==18000 && number(memory,"min_free")==12000);
    assert(heap_reads==1); /* No background sampling or per-sleep heap query. */
    assert(strstr(cJSON_GetObjectItemCaseSensitive(o,"locks")->valuestring,"lock0 NO_LIGHT_SLEEP active=0 held_ms=3500"));
    assert(!strstr(cJSON_GetObjectItemCaseSensitive(o,"locks")->valuestring,"Mode stats"));
    cJSON_Delete(o);
    o=vibe_power_reply("reset",true);assert(number(o,"blank_ms")==0 && number(o,"sleeps")==0);
    assert(number(cJSON_GetObjectItemCaseSensitive(o,"heap"),"min_free")==12000);cJSON_Delete(o);
    clock_us+=1000000;vibe_power_screen(true);clock_us+=2000000;
    o=vibe_power_reply("after",false);assert(number(o,"blank_ms")==1000 && number(o,"elapsed_ms")==3000);cJSON_Delete(o);
    usb=true;lock_rows=100;
    o=vibe_power_reply("bounded",false);char *wire=cJSON_PrintUnformatted(o);
    assert(wire && strlen(wire)<=1536);
    /* Leave room for the retained radio checkpoint, maximum request ID and
     * transport boot/lease envelope; an oversized reply is silently dropped. */
    assert(strlen(wire)+256+64+48<=1536);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"usb_host")));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"locks_truncated")));
    free(wire);cJSON_Delete(o);
    assert(heap_reads==4);
    puts("Actual power diagnostics: sleep/reset accounting, on-demand heap snapshot and bounded lock dump PASS");
}
