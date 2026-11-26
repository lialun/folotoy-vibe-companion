#include "voice_power.h"
#include "esp_pm.h"
#include "esp_log.h"

static esp_pm_lock_handle_t cpu;
static bool held;

void vibe_voice_power(bool active) {
    if (active==held) return;
    if (active && !cpu) {
        esp_err_t result=esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"voice",&cpu);
        if (result!=ESP_OK) {
            ESP_LOGW("vibe_power","Voice CPU reservation unavailable: %s",esp_err_to_name(result));
            return; /* Performance headroom must not disable voice input. */
        }
    }
    esp_err_t result=active?esp_pm_lock_acquire(cpu):esp_pm_lock_release(cpu);
    if (result==ESP_OK) held=active;
    else ESP_LOGW("vibe_power","Voice CPU transition failed: %s",esp_err_to_name(result));
}
