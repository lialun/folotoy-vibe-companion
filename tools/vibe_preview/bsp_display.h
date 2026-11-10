#pragma once
#include <stdbool.h>
#include <stdint.h>
bool bsp_lvgl_lock(uint32_t timeout);
void bsp_lvgl_unlock(void);
