#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Small, bounded credential bank; passwords never enter LAN settings replies. */
#define VIBE_WIFI_PROFILE_MAX 5
#define VIBE_WIFI_BLOB_MAX (6 + VIBE_WIFI_PROFILE_MAX * (2 + 32 + 63))
typedef struct { char ssid[33],password[64]; } vibe_wifi_credential_t;
typedef struct {
    uint8_t count,preferred;
    vibe_wifi_credential_t entries[VIBE_WIFI_PROFILE_MAX];
} vibe_wifi_profiles_t;
typedef bool (*vibe_wifi_persist_fn)(const void *blob,size_t size,void *context);

bool vibe_wifi_credential_valid(const vibe_wifi_credential_t *entry);
int vibe_wifi_profiles_find(const vibe_wifi_profiles_t *profiles,const char *ssid);
bool vibe_wifi_profiles_put(vibe_wifi_profiles_t *profiles,const vibe_wifi_credential_t *entry);
bool vibe_wifi_profiles_remove(vibe_wifi_profiles_t *profiles,const char *ssid);
size_t vibe_wifi_profiles_encode(const vibe_wifi_profiles_t *profiles,void *blob,size_t capacity);
bool vibe_wifi_profiles_decode(vibe_wifi_profiles_t *profiles,const void *blob,size_t size);
/* Persist first; failure never replaces the in-memory authoritative bank. */
bool vibe_wifi_profiles_commit(vibe_wifi_profiles_t *current,const vibe_wifi_profiles_t *next,
                              vibe_wifi_persist_fn persist,void *context);
unsigned vibe_wifi_retry_index(unsigned count,unsigned preferred,unsigned attempt);
unsigned vibe_wifi_retry_delay_ms(unsigned failed_rounds);
