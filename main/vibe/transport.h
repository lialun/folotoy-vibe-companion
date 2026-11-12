#pragma once
#include "hosts.h"
#include "esp_err.h"
esp_err_t vibe_transport_init(void);
uint32_t vibe_transport_boot(void);
vibe_route_t vibe_transport_route(void);
void vibe_transport_owner(vibe_route_t route,uint32_t lease);
void vibe_transport_reply(const char *json,vibe_route_t route);
void vibe_transport_flush(void);
void vibe_transport_reserve_voice(void);
bool vibe_transport_voice_begin(void);
void vibe_transport_voice_end(void);
int vibe_wifi_send(vibe_route_t route,unsigned char type,const void *bytes,size_t size);
