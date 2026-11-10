#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>
typedef esp_err_t (*esp_pm_light_sleep_cb_t)(int64_t,void *);
typedef struct {
    esp_pm_light_sleep_cb_t enter_cb,exit_cb;
    void *enter_cb_user_arg,*exit_cb_user_arg;
    uint32_t enter_cb_prior,exit_cb_prior;
} esp_pm_sleep_cbs_register_config_t;
esp_err_t esp_pm_light_sleep_register_cbs(esp_pm_sleep_cbs_register_config_t *);
esp_err_t esp_pm_dump_locks(FILE *);
