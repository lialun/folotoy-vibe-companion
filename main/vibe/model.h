#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "appearance.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Logical names follow the product: UP, DOWN, SETTINGS (BSP calls it OK). */
typedef enum { V_UP, V_DOWN, V_SETTINGS } vibe_key_t;
typedef enum { V_PRESS, V_RELEASE, V_CLICK, V_LONG } vibe_event_t;
typedef enum { V_HOME, V_POMODORO, V_FUNCTIONS, V_SETTINGS_PAGE, V_RADIO_EXIT, V_PROVISION,
    V_CONNECT_GUIDE, V_COMPUTER_GUIDE } vibe_page_t;
typedef enum { V_TIMER_READY, V_TIMER_RUNNING, V_TIMER_PAUSED, V_TIMER_FINISHED, V_TIMER_ARMED } vibe_timer_t;
typedef enum { V_SETTING_BRIGHTNESS, V_SETTING_VOLUME, V_SETTING_SCREEN_TIMEOUT,
    V_SETTING_THEME, V_SETTING_CONNECT, V_SETTING_WIFI, V_SETTING_UNPAIR, V_SETTING_COUNT } vibe_setting_t;
enum { V_START_AUDIO=1, V_STOP_AUDIO=2, V_ENTER=4, V_ALARM=8,
       V_PREFS_CHANGED=16, V_START_PROVISION=32, V_RADIO_CHANGED=64, V_UNPAIR=256, V_RADIO_STATION=512, V_AGENT_ALERT=1024,
       V_DISMISS_INTRO=2048, V_FINISH_PROVISION=4096 };
typedef struct {
    uint32_t voice_request;
    vibe_page_t page;
    vibe_timer_t timer;
    bool recording, voice_ready, enter_ready, screen_on, alarm_pending, radio_requested, radio_active, switch_radio;
    bool suppress_click[3], wake_only[3], long_seen[3], wake_enter, timer_break, reset_confirm, editing;
    unsigned selected, completed, focus_minutes, break_minutes, radio_index, radio_count;
    unsigned phase_minutes; /* Snapshot the active phase; preference edits affect the next phase. */
    unsigned brightness, volume, screen_seconds;
    unsigned weather_page,info_pages;
    bool info_blocked;
    uint64_t info_rotated_ms;
    bool intro_dismissed, connection_from_menu, guide_wifi, provision_to_ble;
    vibe_page_t provision_return;
    vibe_theme_mode_t theme_mode;
    uint64_t recording_since_ms, deadline_ms, remaining_ms, completion_until_ms, last_activity_ms;
} vibe_model_t;
void vibe_model_init(vibe_model_t *m, uint64_t now);
void vibe_model_connection_boot(vibe_model_t *m,bool paired,bool skipped);
bool vibe_model_connection_page(vibe_page_t page);
void vibe_model_connection_complete(vibe_model_t *m,bool connected,uint64_t now);
void vibe_model_intro_saved(vibe_model_t *m,bool saved,uint64_t now);
void vibe_model_provision_open(vibe_model_t *m,uint64_t now);
void vibe_model_provision_closed(vibe_model_t *m,bool wifi_saved,bool connected,uint64_t now);
unsigned vibe_model_key(vibe_model_t *m, vibe_key_t key, vibe_event_t event, uint64_t now);
unsigned vibe_model_tick(vibe_model_t *m, uint64_t now);
unsigned vibe_model_wait_ms(const vibe_model_t *m, uint64_t now);
uint64_t vibe_model_remaining(const vibe_model_t *m, uint64_t now);
bool vibe_model_completion_visible(const vibe_model_t *m, uint64_t now);
#ifdef __cplusplus
}
#endif
