#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Names are presentation only. Credentials and IDs survive either transport. */
#define VIBE_HOSTS_MAX 8
typedef enum { V_LINK_NONE, V_LINK_WIFI, V_LINK_BLE } vibe_link_t;
typedef struct { uint32_t address; uint16_t port, handle; uint32_t serial; vibe_link_t kind; } vibe_route_t;
typedef struct { char id[33], name[64], token[65]; } vibe_host_t;
typedef struct { uint32_t version, count; vibe_host_t hosts[VIBE_HOSTS_MAX]; } vibe_hosts_saved_t;
typedef struct { vibe_route_t route; uint64_t seen; unsigned timeout_ms; bool audio, hotkey; } vibe_host_link_t;
typedef struct {
    vibe_hosts_saved_t saved;
    vibe_host_link_t links[VIBE_HOSTS_MAX][2];
    int owner;
    vibe_link_t selected;
    uint32_t lease;
} vibe_hosts_t;

bool vibe_host_hex(const char *value,size_t length);
bool vibe_route_equal(vibe_route_t a,vibe_route_t b);
bool vibe_hosts_decode(vibe_hosts_saved_t *out,const void *bytes,size_t size);
void vibe_hosts_init(vibe_hosts_t *h,const vibe_hosts_saved_t *saved,const char *legacy_token);
int vibe_hosts_auth(const vibe_hosts_t *h,const char *id,const char *token);
/* Pure proposed update: persist successfully before publishing it. */
int vibe_hosts_register(vibe_hosts_saved_t *next,const char *id,const char *name,const char *token,int authenticated);
/* Only after the physical pairing code has been verified: recover this ID's
 * credential without erasing other computers. Persist next before publishing. */
int vibe_hosts_pair(vibe_hosts_saved_t *next,const char *id,const char *name,const char *token);
void vibe_hosts_seen(vibe_hosts_t *h,int host,vibe_route_t route,uint64_t now,unsigned timeout_ms,bool audio,bool hotkey);
void vibe_hosts_lost(vibe_hosts_t *h,vibe_route_t route);
void vibe_hosts_disable(vibe_hosts_t *h,vibe_link_t kind);
void vibe_hosts_poll(vibe_hosts_t *h,uint64_t now);
bool vibe_hosts_claim(vibe_hosts_t *h,int host,vibe_route_t via,uint64_t now);
vibe_host_link_t vibe_hosts_active(const vibe_hosts_t *h,uint64_t now);
bool vibe_hosts_controls(const vibe_hosts_t *h,int host,vibe_route_t route,uint64_t now);
