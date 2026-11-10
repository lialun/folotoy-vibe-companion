#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct {
    uint32_t lightsleep_cnt;
    uint64_t sleep_in_rtc_time_stamp,sleep_out_rtc_time_stamp;
    uint32_t wakeup_triggers,sleep_flags;
    esp_err_t sleep_request_result;
} esp_sleep_context_t;
void esp_sleep_set_sleep_context(esp_sleep_context_t *);
