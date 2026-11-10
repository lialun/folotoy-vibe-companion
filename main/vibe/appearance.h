#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Persisted in the versioned preferences snapshot, separately from legacy layout. */
typedef enum { V_THEME_DARK, V_THEME_LIGHT, V_THEME_SCHEDULED } vibe_theme_mode_t;
typedef struct { uint32_t mode,light_start,dark_start; int32_t tz_minutes; } vibe_appearance_t;
vibe_appearance_t vibe_appearance_default(void);
bool vibe_appearance_valid(const vibe_appearance_t *a);
bool vibe_appearance_light(const vibe_appearance_t *a,int64_t epoch,int tz_minutes);
#ifdef __cplusplus
}
#endif
