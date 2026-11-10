#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIBE_PORTAL_IP "192.168.4.1"
#define VIBE_PORTAL_URL "http://192.168.4.1/"
struct sockaddr;
bool vibe_portal_local_address(const struct sockaddr *address,size_t length);
bool vibe_portal_host(const char *host);
bool vibe_portal_origin(const char *origin);
bool vibe_portal_close_due(bool wifi_connected,uint64_t saved_at,uint64_t now);
size_t vibe_portal_dns_reply(const uint8_t *query,size_t length,uint8_t *reply,size_t capacity);
