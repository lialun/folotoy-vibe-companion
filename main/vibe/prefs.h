#pragma once
#include "appearance.h"
#include <stddef.h>

/* Keep this legacy layout unchanged: installed devices store it as a raw NVS blob. */
typedef struct {
    unsigned brightness, volume, screen_seconds, focus_minutes, break_minutes, mic_gain;
    double latitude, longitude;
    bool weather_enabled;
    char city[49], radio_url[257], radio_name[49];
} vibe_prefs_t;
typedef struct {
    uint32_t version;
    vibe_prefs_t prefs;
    vibe_appearance_t appearance;
} vibe_saved_prefs_t;
#define VIBE_PREFS_VERSION 3
vibe_prefs_t vibe_prefs_default(void);
void vibe_prefs_require_weather(vibe_prefs_t *prefs);
bool vibe_prefs_decode(const void *bytes,size_t size,bool legacy,vibe_prefs_t *prefs,vibe_appearance_t *appearance);
/* A validated volume-only change is safe during playback or capture. */
bool vibe_prefs_volume_only(const vibe_prefs_t *before,const vibe_prefs_t *after,
                            const vibe_appearance_t *old_theme,const vibe_appearance_t *new_theme);
