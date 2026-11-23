#pragma once
#include "model.h"
#include "weather.h"
#include "appearance.h"
#include "signal.h"
#include "prefs.h"
#include "messages.h"
#include "connection_request.h"
#include "agent_alert.h"
#include "quota.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VIBE_PORT 33333
typedef struct {
    vibe_connection_request_t connection_request;
    bool ble_peer, wifi, peer, audio_ready, hotkey_ready, audio_busy, audio_fault, provisioning;
    bool radio_playing, radio_loading, radio_pcm, voice_v2;
    unsigned radio_state;
    char radio_name[64], radio_description[96],radio_error[64];
    vibe_weather_t forecast;
    vibe_air_t air;
    int rssi, battery, tz_minutes;
    vibe_signal_t signal;
    vibe_agent_alert_t agent_alert;
    vibe_quota_card_t quota;
    unsigned quota_pages,quota_part,quota_parts;
    vibe_appearance_t appearance;
    unsigned drops;
    uint32_t settings_revision;
    unsigned heartbeat_ms;
    char ip[16], weather[96], notice[96], ap_name[33];
    char token[65], device_id[18];
    uint64_t peer_seen_ms, weather_at_ms, weather_attempt_ms, air_at_ms, air_attempt_ms;
} vibe_status_t;
extern vibe_model_t v_model;
extern vibe_prefs_t v_prefs;
extern vibe_status_t v_status;
extern vibe_quota_store_t v_quotas;
extern SemaphoreHandle_t v_mutex;
extern QueueHandle_t v_queue;
uint64_t vibe_now(void);
void vibe_snapshot(vibe_model_t *m, vibe_prefs_t *p, vibe_status_t *s);
bool vibe_save_prefs(void);
void vibe_send_json(const char *json);
void vibe_send_json_both(const char *json);
uint32_t vibe_network_generation(void);
int vibe_send_audio(const void *data, size_t len);
/* V2 uplink: immediate UDP plus a bounded, independently drained BLE copy. */
int vibe_send_voice(const void *data,size_t len,uint32_t owner);
esp_err_t vibe_network_init(void);
void vibe_network_wake(void);
void vibe_network_release(void);
/* Called by the controller under v_mutex, only after credential persistence. */
void vibe_network_commit(const char *client);
bool vibe_connection_approved(const char *token,const char *client,const char *request);
bool vibe_network_generation_matches(uint32_t generation);
/* One disposable weather worker. Audio pauses and joins it before taking RAM. */
void vibe_weather_poll(uint64_t now);
void vibe_weather_pause(void);
void vibe_weather_resume(void);
bool vibe_provision_start(void);
bool vibe_provision_finish(void);
bool vibe_provision_connected(void);
bool vibe_audio_request(unsigned action);
esp_err_t vibe_audio_init(void);
void vibe_ui_init(void);
void vibe_ui_refresh(void);
/* NULL: lock-free capture from the button callback. Non-NULL: controller-only,
 * return a presented frame only when it belongs to this exact request UUID. */
uint32_t vibe_ui_connection_frame(const char *request);
bool vibe_json_depth_ok(const char *s, size_t len);
#ifdef __cplusplus
}
#endif
