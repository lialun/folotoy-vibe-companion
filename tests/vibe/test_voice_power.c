#include "voice_power.h"
#include "esp_pm.h"
#include <assert.h>
#include <stdio.h>

struct voice_test_pm_lock { unsigned references; };
static struct voice_test_pm_lock lock;
static unsigned creates,acquires,releases;
static bool fail_create,fail_acquire;

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type,int arg,const char *name,esp_pm_lock_handle_t *out) {
    assert(type==ESP_PM_CPU_FREQ_MAX && !arg && name && *name);
    ++creates;
    if(fail_create)return ESP_ERR_NO_MEM;
    *out=&lock;return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t value) {
    assert(value==&lock && !lock.references);++acquires;
    if(fail_acquire)return ESP_FAIL;
    ++lock.references;return ESP_OK;
}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t value) {
    assert(value==&lock && lock.references==1);++releases;
    --lock.references;return ESP_OK;
}
int main(void) {
    vibe_voice_power(false);assert(!creates && !lock.references);
    fail_create=true;vibe_voice_power(true);vibe_voice_power(false);
    assert(creates==1 && !acquires && !releases);
    fail_create=false;fail_acquire=true;vibe_voice_power(true);vibe_voice_power(false);
    assert(creates==2 && acquires==1 && !releases && !lock.references);
    fail_acquire=false;
    for(unsigned cycle=0;cycle<100;cycle++) {
        vibe_voice_power(true);vibe_voice_power(true);
        assert(lock.references==1 && creates==2 && acquires==cycle+2);
        vibe_voice_power(false);vibe_voice_power(false);
        assert(!lock.references && releases==cycle+1);
    }
    puts("Voice CPU adapter: allocation/acquire fallback, one handle, balanced transitions and idle release PASS");
}
