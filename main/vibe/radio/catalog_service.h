#pragma once
#include <stdbool.h>
typedef struct cJSON cJSON;
#ifdef __cplusplus
extern "C" {
#endif
enum { V_RADIO_STOPPED, V_RADIO_CONNECTING, V_RADIO_BUFFERING, V_RADIO_PLAYING, V_RADIO_ERROR };
typedef enum { V_RADIO_SESSION_FAILED, V_RADIO_SESSION_PLAYED, V_RADIO_SESSION_NO_MEMORY } vibe_radio_result_t;
vibe_radio_result_t vibe_radio_session(const char *url,bool (*cancel)(void));
/* init/sync/request are called by the controller while holding v_mutex. */
void vibe_radio_catalog_init(void);
void vibe_radio_catalog_sync(void);
void vibe_radio_catalog_request(void);
void vibe_radio_remember(void);
void vibe_radio_cancel_transfer(void);
cJSON *vibe_radio_library_request(const cJSON *request);
void vibe_radio_url(char *url,unsigned capacity);
void vibe_radio_played(const char *url);
void vibe_radio_status(unsigned state,const char *detail);
#ifdef __cplusplus
}
#endif
