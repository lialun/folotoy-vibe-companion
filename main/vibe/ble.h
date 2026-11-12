#pragma once
#include "hosts.h"
#include "esp_err.h"
esp_err_t vibe_ble_init(void);
/* Returns the actual running state, including a failed start/stop. */
bool vibe_ble_enable(bool enabled);
void vibe_ble_poll(uint64_t now,bool voice,vibe_route_t active);
int vibe_ble_send(vibe_route_t route,unsigned char type,const void *bytes,size_t size);
