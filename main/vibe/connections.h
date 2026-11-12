#pragma once
#include "hosts.h"
#include "messages.h"
#include "cJSON.h"
/* Controller task only, with v_mutex held (except init before tasks start). */
void vibe_connections_init(const char *legacy_token);
bool vibe_connections_receive(const cJSON *json,vibe_route_t source,const char *request_id);
void vibe_connections_poll(uint64_t now);
void vibe_connections_lost(vibe_route_t source);
void vibe_connections_wifi_lost(void);
bool vibe_connections_clear(void);
uint32_t vibe_connections_lease(void);
bool vibe_connections_wireless(bool wifi,bool ble);
