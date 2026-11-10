// No LVGL task may see the display until the rounding callback is registered.
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "../components/bsp/src/bsp_display_lvgl.c"

static lv_display_t display;
static int panel_present = 1, lock_depth, port_live, display_live, callback_live;
static int fail_lock, fail_port, fail_display, fail_event, init_calls, unlocked_flushes;
static int panel_token, io_token;
static uint32_t (*tick_cb)(void);
static int64_t clock_us;
static int sleep_calls, wake_calls, redraws, task_wakes;
static bool panel_asleep, fail_sleep, fail_wake, fail_off, fail_on;
int64_t esp_timer_get_time(void) { return clock_us; }
void lv_tick_set_cb(uint32_t (*cb)(void)) { assert(lock_depth); tick_cb = cb; }
lv_timer_t *lv_display_get_refr_timer(lv_display_t *d) { assert(d == &display); return &d->refresh; }
void lv_timer_pause(lv_timer_t *t) { assert(lock_depth && t == &display.refresh); t->paused = true; }
void lv_timer_resume(lv_timer_t *t) { assert(lock_depth && !panel_asleep); t->paused = false; }
lv_obj_t *lv_display_get_screen_active(lv_display_t *d) { return &d->screen; }
void lv_obj_invalidate(lv_obj_t *obj) { assert(lock_depth && obj == &display.screen && !panel_asleep); ++redraws; }
esp_err_t lvgl_port_task_wake(int event, void *param) {
    assert(lock_depth && event == LVGL_PORT_EVENT_DISPLAY && !param); ++task_wakes; return ESP_OK;
}
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on) {
    assert(lock_depth && panel == &panel_token && display.refresh.paused);
    return (on ? fail_on : fail_off) ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_lcd_panel_disp_sleep(esp_lcd_panel_handle_t panel, bool asleep) {
    assert(lock_depth && panel == &panel_token && display.refresh.paused);
    if (asleep) { ++sleep_calls; if (fail_sleep) return ESP_FAIL; }
    else { ++wake_calls; if (fail_wake) return ESP_FAIL; }
    panel_asleep = asleep; return ESP_OK;
}
esp_lcd_panel_handle_t bsp_display_panel(void) { return panel_present ? &panel_token : NULL; }
esp_lcd_panel_io_handle_t bsp_display_io(void) { return &io_token; }
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg) {
    assert(cfg->timer_period_ms == 30000 && cfg->task_max_sleep_ms == 30000); ++init_calls; assert(!port_live);
    if (fail_port) return ESP_ERR_NO_MEM;
    port_live = 1; return ESP_OK;
}
esp_err_t lvgl_port_deinit(void) {
    // The real API is asynchronous: returning does not release the context.
    assert(false && "Display rollback must not deinit/reinitialize the live port");
    return ESP_OK;
}
bool lvgl_port_lock(uint32_t timeout) {
    (void)timeout; assert(port_live);
    if (fail_lock) return false;
    ++lock_depth; return true;
}
void lvgl_port_unlock(void) {
    assert(lock_depth > 0);
    --lock_depth;
    if (!lock_depth && display_live && !display.refresh.paused) {
        assert(callback_live); // Simulate rendering as soon as the lock is free.
        assert(!panel_asleep);
        ++unlocked_flushes;
    }
}
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg) {
    assert(lock_depth > 0 && cfg->panel_handle == &panel_token && !display_live);
    assert(cfg->buffer_size==BSP_LCD_W*BSP_LVGL_DRAW_BUFFER_LINES && !cfg->double_buffer);
    assert(lvgl_port_lock(0)); // Real port takes and releases a recursive lock.
    if (!fail_display) display_live = 1;
    lvgl_port_unlock();
    return fail_display ? NULL : &display;
}
esp_err_t lvgl_port_remove_disp(lv_display_t *disp) {
    assert(disp == &display && lock_depth && display_live);
    display_live = callback_live = 0; return ESP_OK;
}
void lv_display_add_event_cb(lv_display_t *disp, void (*cb)(lv_event_t *), int code, void *user) {
    (void)user;
    assert(lock_depth && disp == &display && code == LV_EVENT_FLUSH_START && cb == rounded_flush_event);
    if (!fail_event) callback_live = 1;
}
uint32_t lv_display_get_event_count(lv_display_t *disp) {
    assert(disp == &display && lock_depth);
    return 1 + callback_live; // The display already owns an internal callback.
}
static void expect_failure(void) {
    assert(bsp_lvgl_init() == NULL);
    assert(!s_disp && !lock_depth && !display_live);
    assert(!bsp_lvgl_lock(0));
}
int main(void) {
    assert(bsp_lvgl_set_sleep(true) == ESP_ERR_INVALID_STATE);
    panel_present = 0; expect_failure(); panel_present = 1;
    fail_port = 1; expect_failure(); fail_port = 0;
    const int failed_init_calls = init_calls;
    expect_failure(); // A partially initialized port cannot safely be re-created.
    assert(init_calls == failed_init_calls);
    s_port_init_failed = false; // Simulate reboot for remaining scenarios.
    fail_lock = 1; expect_failure(); fail_lock = 0;
    const int retained_init_calls = init_calls;
    fail_display = 1; expect_failure(); fail_display = 0;
    fail_event = 1; expect_failure(); fail_event = 0;
    assert(bsp_lvgl_init() == &display);
    assert(init_calls == retained_init_calls); // Display retries reuse the port.
    assert(callback_live && !lock_depth && unlocked_flushes == 1);
    const int before = init_calls;
    assert(bsp_lvgl_init() == &display && init_calls == before);
    assert(bsp_lvgl_lock(5)); bsp_lvgl_unlock();
    uint16_t pixels[BSP_LCD_W] = {0};
    for (int x = 0; x < BSP_LCD_W; ++x) pixels[x] = 0xffff;
    display.buffer = (lv_draw_buf_t){ .data = (uint8_t *)pixels, .header.stride = sizeof(pixels) };
    lv_area_t area = { .x1 = 0, .y1 = 0, .x2 = BSP_LCD_W - 1, .y2 = 0 };
    lv_event_t ev = { .target = &display, .area = &area };
    rounded_flush_event(&ev);
    assert(pixels[0] == 0 && pixels[BSP_LCD_W - 1] == 0 && pixels[BSP_LCD_W / 2] == 0xffff);
    // A sparse port tick must not freeze LVGL time or depend on catch-up ticks.
    assert(tick_cb); clock_us = 12345000; assert(tick_cb() == 12345);
    fail_lock = 1; assert(bsp_lvgl_set_sleep(true) == ESP_ERR_TIMEOUT); fail_lock = 0;
    assert(!display.refresh.paused);
    assert(bsp_lvgl_set_sleep(true) == ESP_OK && panel_asleep && display.refresh.paused);
    assert(bsp_lvgl_set_sleep(true) == ESP_OK && sleep_calls == 1);
    clock_us += 60000000; assert(tick_cb() == 72345);
    fail_wake = true; assert(bsp_lvgl_set_sleep(false) == ESP_FAIL); fail_wake = false;
    assert(panel_asleep && display.refresh.paused && !redraws);
    fail_on = true; assert(bsp_lvgl_set_sleep(false) == ESP_FAIL); fail_on = false;
    assert(display.refresh.paused && !redraws);
    assert(bsp_lvgl_set_sleep(false) == ESP_OK && !panel_asleep && !display.refresh.paused);
    assert(redraws == 1 && task_wakes == 1);
    const int wakes = wake_calls;
    assert(bsp_lvgl_set_sleep(false) == ESP_OK && wake_calls == wakes);
    // Incomplete Sleep In must still recover without duplicate port allocation.
    fail_off = true; assert(bsp_lvgl_set_sleep(true) == ESP_FAIL); fail_off = false;
    assert(display.refresh.paused);
    assert(bsp_lvgl_set_sleep(false) == ESP_OK && !display.refresh.paused);
    fail_sleep = true; assert(bsp_lvgl_set_sleep(true) == ESP_FAIL); fail_sleep = false;
    assert(display.refresh.paused);
    assert(bsp_lvgl_set_sleep(true) == ESP_OK);
    assert(bsp_lvgl_set_sleep(false) == ESP_OK);
    for (int i = 0; i < 64; ++i) {
        assert(bsp_lvgl_set_sleep(true) == ESP_OK);
        clock_us += 15000000;
        assert(bsp_lvgl_set_sleep(false) == ESP_OK);
    }
    assert(init_calls == before && !lock_depth && task_wakes == redraws);
    puts("BSP LVGL initialization tests: PASS");
}
