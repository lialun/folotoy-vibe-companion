#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "power.h"
#include "power_stats.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_sleep_internal.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

static esp_sleep_context_t sleep_context;
static vibe_power_stats_t stats;
static uint32_t last_attempt;
static portMUX_TYPE stats_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t IRAM_ATTR sleep_exit(int64_t elapsed, void *arg) {
    (void)arg;
    /* The exit callback also runs when PM skipped a short request. The SDK
     * context distinguishes those from real successful/rejected attempts. */
    if (sleep_context.lightsleep_cnt != last_attempt) {
        last_attempt = sleep_context.lightsleep_cnt;
        portENTER_CRITICAL(&stats_mux);
        vibe_power_stats_sleep(&stats, sleep_context.sleep_request_result == ESP_OK,
                               elapsed > 0 ? (uint64_t)elapsed : 0);
        portEXIT_CRITICAL(&stats_mux);
    }
    return ESP_OK;
}

esp_err_t vibe_power_init(void) {
    stats.started_us = esp_timer_get_time();
    esp_sleep_set_sleep_context(&sleep_context);
    esp_pm_sleep_cbs_register_config_t callbacks = {.exit_cb = sleep_exit};
    return esp_pm_light_sleep_register_cbs(&callbacks);
}
void vibe_power_screen(bool awake) {
    portENTER_CRITICAL(&stats_mux);
    vibe_power_stats_screen(&stats, awake, esp_timer_get_time());
    portEXIT_CRITICAL(&stats_mux);
}

/* Compress the SDK's lock dump on demand. Keep lock acquisition history, but
 * omit headings/mode tables. No private lock layout or persistent log buffer. */
typedef struct { char line[160], text[768]; unsigned used, written; bool truncated; } lock_text_t;
static int lock_write(void *cookie, const char *bytes, int length) {
    lock_text_t *s = cookie;
    for (int i=0; i<length; ++i) {
        if (bytes[i] != '\n') {
            if (s->used+1 < sizeof(s->line)) s->line[s->used++] = bytes[i];
            continue;
        }
        s->line[s->used] = 0;
        char name[32], type[32]; int argument; unsigned active, taken; unsigned long long held;
        if (sscanf(s->line, "%31s %31s %d %u %u %llu", name, type, &argument, &active, &taken, &held)==6 &&
            (!strcmp(type,"NO_LIGHT_SLEEP") || !strcmp(type,"APB_FREQ_MAX") || !strcmp(type,"CPU_FREQ_MAX"))) {
            int n=snprintf(s->text+s->written,sizeof(s->text)-s->written,
                           "%s %s active=%u held_ms=%llu\n",name,type,active,held/1000);
            if (n>=0 && (unsigned)n<sizeof(s->text)-s->written) s->written+=(unsigned)n;
            else s->truncated=true;
        }
        s->used=0;
    }
    return length;
}
#ifdef __GLIBC__
static ssize_t lock_cookie_write(void *cookie,const char *bytes,size_t length) {
    return lock_write(cookie,bytes,(int)length);
}
#endif
cJSON *vibe_power_reply(const char *id, bool reset) {
    /* Reuse allocator counters, before allocating the diagnostic response.
     * Match the 8-bit heap used for audio/decoder/cache allocations. */
    multi_heap_info_t heap;
    heap_caps_get_info(&heap,MALLOC_CAP_8BIT);
    uint64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&stats_mux);
    if (reset) {
        bool blank=stats.blank;
        stats=(vibe_power_stats_t){.started_us=now,.blank=blank,.blank_since_us=now};
    }
    vibe_power_stats_t s=stats;
    portEXIT_CRITICAL(&stats_mux);
    if (s.blank && now>=s.blank_since_us) s.blank_us+=now-s.blank_since_us;
    cJSON *o=cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddStringToObject(o,"event","power.reply"); cJSON_AddStringToObject(o,"id",id);
    cJSON_AddNumberToObject(o,"elapsed_ms",(now-s.started_us)/1000);
    cJSON_AddNumberToObject(o,"sleep_ms",s.sleep_us/1000);
    cJSON_AddNumberToObject(o,"blank_ms",s.blank_us/1000);
    cJSON_AddNumberToObject(o,"blank_sleep_ms",s.blank_sleep_us/1000);
    cJSON_AddNumberToObject(o,"longest_ms",s.longest_us/1000);
    cJSON_AddNumberToObject(o,"sleeps",s.sleeps); cJSON_AddNumberToObject(o,"rejects",s.rejects);
    cJSON_AddBoolToObject(o,"usb_host",usb_serial_jtag_is_connected());
    cJSON_AddBoolToObject(o,"key_wake",bsp_button_idle_wakeup_armed());
    cJSON_AddNumberToObject(o,"battery",bsp_battery_soc());
    cJSON_AddNumberToObject(o,"millivolts",bsp_battery_mv());
    cJSON *memory=cJSON_AddObjectToObject(o,"heap");
    if (memory) {
        cJSON_AddNumberToObject(memory,"free",heap.total_free_bytes);
        cJSON_AddNumberToObject(memory,"used",heap.total_allocated_bytes);
        cJSON_AddNumberToObject(memory,"largest",heap.largest_free_block);
        cJSON_AddNumberToObject(memory,"min_free",heap.minimum_free_bytes);
    }
    lock_text_t locks={0};
#ifdef __GLIBC__
    FILE *stream=fopencookie(&locks,"w",(cookie_io_functions_t){.write=lock_cookie_write});
#else
    FILE *stream=funopen(&locks,NULL,lock_write,NULL,NULL);
#endif
    if (stream) {
        setvbuf(stream,NULL,_IONBF,0);
        esp_pm_dump_locks(stream); fclose(stream);
        cJSON_AddStringToObject(o,"locks",locks.text);
        cJSON_AddBoolToObject(o,"locks_truncated",locks.truncated);
    }
    return o;
}
