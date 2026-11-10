#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int filtered; unsigned bars; bool valid; } vibe_signal_t;
/* Half-weight EMA in 1/256 dBm units; 3 dBm hysteresis. Invalid samples do not overwrite a valid one. */
void vibe_signal_update(vibe_signal_t *state,int rssi);
#ifdef __cplusplus
}
#endif
