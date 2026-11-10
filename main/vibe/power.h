#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "cJSON.h"
esp_err_t vibe_power_init(void);
void vibe_power_screen(bool awake);
/* Caller owns the response. Reading/resetting never writes Flash. */
cJSON *vibe_power_reply(const char *id, bool reset);
