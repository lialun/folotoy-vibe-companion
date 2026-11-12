#pragma once
#include "hosts.h"
#include "esp_err.h"
typedef struct cJSON cJSON;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t vibe_relay_init(void);
/* No v_mutex or NimBLE APIs in receive; safe in either transport callback. */
bool vibe_relay_receive(vibe_route_t route,const void *bytes,size_t size);
/* Controller holds v_mutex. Abort is also used to fence an owner/route change. */
void vibe_relay_abort(void);
void vibe_relay_control(const cJSON *body);
/* Audio worker owns allocation, codec and cleanup; reuses resident voice PCM. */
int vibe_relay_play(const char *url,bool (*cancel)(void),int16_t *pcm);
#ifdef __cplusplus
}
#endif
