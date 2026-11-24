#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
esp_err_t vibe_ble_init(void);
/* Worker-only; bounded lock/pool wait shares one 30 ms voice / 50 ms control budget. */
int vibe_ble_send_to(uint16_t handle,unsigned char type,const void *bytes,size_t length);
void vibe_ble_publish_metadata(const char *text,size_t length);
/* These entry points share the LAN control-owner policy. */
bool vibe_ble_generation_matches(uint16_t handle,uint32_t generation);
uint32_t vibe_ble_generation(uint16_t handle);
void vibe_receive_ble(uint16_t handle,uint32_t generation,const void *packet,size_t length);
void vibe_ble_disconnected(uint16_t handle);
int vibe_discovery_json(char *buffer,size_t capacity,bool authorized,bool auth_checked);
