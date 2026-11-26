#pragma once
#include <stdbool.h>

typedef enum { V_WIFI_POWER_UNCHANGED, V_WIFI_POWER_MODEM, V_WIFI_POWER_AWAKE } vibe_wifi_power_action_t;
typedef struct { bool valid,awake,access_point; } vibe_wifi_power_t;

/* Voice tail and radio have overlapping lifetimes. Neither may put Wi-Fi to
 * sleep while the other still owns real-time traffic. Only transitions cause
 * an SDK call; a stopped driver invalidates the cached request. */
static inline vibe_wifi_power_action_t vibe_wifi_power_next(vibe_wifi_power_t *state,
        bool started,bool access_point,bool voice,bool radio) {
    if (!started) {state->valid=false;return V_WIFI_POWER_UNCHANGED;}
    bool awake=voice || radio;
    if (state->valid && state->awake==awake && state->access_point==access_point)
        return V_WIFI_POWER_UNCHANGED;
    *state=(vibe_wifi_power_t){.valid=true,.awake=awake,.access_point=access_point};
    return awake?V_WIFI_POWER_AWAKE:V_WIFI_POWER_MODEM;
}
