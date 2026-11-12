#pragma once
#include "esp_err.h"
typedef void *esp_pm_lock_handle_t;
#define ESP_PM_NO_LIGHT_SLEEP 2
esp_err_t esp_pm_lock_create(int,int,const char *,esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
