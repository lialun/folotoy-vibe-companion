// Execute the real BSP with public-interface stubs and a fake ADC (no SDK needed).
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "../components/bsp/src/bsp_button.c"

struct button_dev_t { button_driver_t *driver; bool live; };
static struct button_dev_t buttons[BSP_BTN_COUNT];
static int adc_token, cal_token, adc_live, cal_live, live_buttons;
static int create_calls, callback_calls, fail_create, fail_callback;
static int fail_adc, fail_channel, fail_cal, fail_read, fail_convert, fail_delete;
static int raw_mv, reads, events;
static int64_t clock_us;
static gpio_isr_t wake_isr;
static bool digital_input, wake_enabled, irq_enabled, polling;
static int fail_wake, fail_isr;
esp_err_t gpio_input_enable(gpio_num_t pin) { assert(pin==BSP_BTN_GPIO);digital_input=true;return ESP_OK; }
esp_err_t gpio_input_disable(gpio_num_t pin) { assert(pin==BSP_BTN_GPIO);digital_input=false;return ESP_OK; }
esp_err_t gpio_wakeup_enable(gpio_num_t pin,gpio_int_type_t type) {
    assert(pin==BSP_BTN_GPIO && type==GPIO_INTR_LOW_LEVEL);
    if (fail_wake) return ESP_FAIL;
    wake_enabled=true;return ESP_OK;
}
esp_err_t gpio_wakeup_disable(gpio_num_t pin) { assert(pin==BSP_BTN_GPIO);wake_enabled=false;return ESP_OK; }
esp_err_t gpio_intr_enable(gpio_num_t pin) { assert(pin==BSP_BTN_GPIO);irq_enabled=true;return ESP_OK; }
esp_err_t gpio_intr_disable(gpio_num_t pin) { assert(pin==BSP_BTN_GPIO);irq_enabled=false;return ESP_OK; }
esp_err_t gpio_set_intr_type(gpio_num_t pin,gpio_int_type_t type) {
    assert(pin==BSP_BTN_GPIO && type==GPIO_INTR_LOW_LEVEL);return ESP_OK;
}
esp_err_t gpio_install_isr_service(int flags) { assert(flags==ESP_INTR_FLAG_IRAM);return fail_isr?ESP_FAIL:ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t pin,gpio_isr_t handler,void *arg) {
    assert(pin==BSP_BTN_GPIO && !arg && !wake_isr);wake_isr=handler;return ESP_OK;
}
esp_err_t gpio_isr_handler_remove(gpio_num_t pin) { assert(pin==BSP_BTN_GPIO);wake_isr=NULL;return ESP_OK; }
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
esp_err_t iot_button_resume(void) { polling=true;return ESP_OK; }
void iot_button_power_save_wakeup_isr(uint32_t gpio) {
    assert(gpio==BSP_BTN_GPIO && digital_input && wake_enabled && irq_enabled);
    polling=true;gpio_intr_disable(gpio);
}

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg, adc_oneshot_unit_handle_t *h) {
    assert(cfg->unit_id == BSP_BTN_ADC_UNIT && !adc_live);
    if (fail_adc) return ESP_ERR_NO_MEM;
    adc_live = 1; *h = &adc_token; return ESP_OK;
}
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t h) {
    assert(h == &adc_token && adc_live && !live_buttons);
    adc_live = 0; return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t h, int channel, const adc_oneshot_chan_cfg_t *cfg) {
    assert(h == &adc_token && channel == BSP_BTN_ADC_CHANNEL && cfg->atten == ADC_ATTEN_DB_12);
    return fail_channel ? ESP_FAIL : ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t h, int channel, int *raw) {
    assert(h == &adc_token && adc_live && channel == BSP_BTN_ADC_CHANNEL);
    ++reads;
    if (fail_read) return ESP_FAIL;
    *raw = raw_mv; return ESP_OK;
}
esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *cfg, adc_cali_handle_t *h) {
    assert(adc_live && !cal_live && cfg->atten == ADC_ATTEN_DB_12);
    if (fail_cal) return ESP_ERR_NO_MEM;
    cal_live = 1; *h = &cal_token; return ESP_OK;
}
esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t h) {
    assert(h == &cal_token && cal_live && !live_buttons);
    cal_live = 0; return ESP_OK;
}
esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t h, int raw, int *mv) {
    assert(h == &cal_token && cal_live);
    if (fail_convert) { *mv = 0; return ESP_FAIL; }
    *mv = raw; return ESP_OK;
}
int64_t esp_timer_get_time(void) { return clock_us; }

// GPIO + delay stubs for the deep-sleep hand-off (see bsp_button_prepare_deep_sleep).
static int gpio_direction_calls, gpio_pull_calls, gpio_level_calls, delay_calls, fail_gpio;
static gpio_num_t gpio_last_pin;
static gpio_mode_t gpio_last_mode;
static gpio_pull_mode_t gpio_last_pull;
static int gpio_level = 1;

esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode) {
    if (mode==GPIO_MODE_DISABLE) { digital_input=false;return ESP_OK; }
    ++gpio_direction_calls; gpio_last_pin = pin; gpio_last_mode = mode;
    return fail_gpio ? ESP_FAIL : ESP_OK;
}
esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t pull) {
    ++gpio_pull_calls; gpio_last_pin = pin; gpio_last_pull = pull;
    return fail_gpio ? ESP_FAIL : ESP_OK;
}
int gpio_get_level(gpio_num_t pin) {
    (void)pin; ++gpio_level_calls; return gpio_level;
}
void vTaskDelay(TickType_t ticks) {
    (void)ticks; ++delay_calls;
}
esp_err_t iot_button_create(const button_config_t *cfg, const button_driver_t *driver, button_handle_t *h) {
    // 判定门限必须由 BSP 显式下发(bsp_pins.h),不能退回组件默认的 180 / 1500ms。
    assert(cfg->short_press_time == BSP_BTN_SHORT_PRESS_MS);
    assert(cfg->long_press_time == BSP_BTN_LONG_PRESS_MS);
    if (++create_calls == fail_create) return ESP_ERR_NO_MEM;
    for (int i = 0; i < BSP_BTN_COUNT; ++i) {
        if (buttons[i].live) continue;
        buttons[i] = (struct button_dev_t){ .driver = (button_driver_t *)driver, .live = true };
        *h = &buttons[i]; ++live_buttons;
        assert(driver->get_key_level((button_driver_t *)driver) == BUTTON_INACTIVE);
        return ESP_OK;
    }
    assert(false); return ESP_FAIL;
}
esp_err_t iot_button_delete(button_handle_t h) {
    assert(h && h->live && adc_live && cal_live);
    if (fail_delete) return ESP_FAIL;
    assert(h->driver->del(h->driver) == ESP_OK);
    h->live = false; --live_buttons; return ESP_OK;
}
esp_err_t iot_button_register_cb(button_handle_t h, button_event_t ev, button_event_args_t *args, button_cb_t cb, void *u) {
    (void)args; (void)ev;
    assert(h->live);
    cb(h, u); // No user callbacks may escape a partial initialization.
    return ++callback_calls == fail_callback ? ESP_ERR_NO_MEM : ESP_OK;
}
static void event_cb(bsp_btn_t btn, bsp_btn_ev_t ev, void *u) {
    assert(btn == BSP_BTN_OK && ev == BSP_BTN_CLICK && u == &events);
    ++events;
}
static void reset_faults(void) {
    fail_adc = fail_channel = fail_cal = fail_read = fail_convert = fail_delete = 0;
    fail_create = fail_callback = create_calls = callback_calls = 0;
}
static void assert_clean(void) {
    assert(!adc_live && !cal_live && !live_buttons && !s_ready && !s_adc && !s_cali);
    for (int i = 0; i < BSP_BTN_COUNT; ++i) assert(!s_btn[i]);
}

// The board hands the shared ADC key pad back to a plain digital input before
// deep sleep: the drivers and the ADC must be released, the pin must get its
// pull-up back, and the sampled level must be reported so the caller can skip
// sleeping while a key is still held down.
static void check_deep_sleep_prepare(void) {
    reset_faults();
    gpio_direction_calls = gpio_pull_calls = gpio_level_calls = delay_calls = 0;
    fail_gpio = 0; gpio_level = 1;
    assert(bsp_button_init(event_cb, &events) == ESP_OK);

    int level = -1;
    assert(bsp_button_prepare_deep_sleep(&level) == ESP_OK);
    assert(level == 1);
    assert(gpio_direction_calls == 1 && gpio_pull_calls == 1 && gpio_level_calls == 1);
    assert(delay_calls == 1);                       // let the pull-up settle first
    assert(gpio_last_pin == BSP_BTN_GPIO);
    assert(gpio_last_mode == GPIO_MODE_INPUT);
    assert(gpio_last_pull == GPIO_PULLUP_ONLY);
    assert_clean();                                 // nothing may touch the pad afterwards

    // A key still held low is reported as 0 so the caller can refuse to sleep.
    gpio_level = 0; level = -1;
    assert(bsp_button_prepare_deep_sleep(&level) == ESP_OK);
    assert(level == 0);

    // A pin failure is propagated instead of being reported as an idle pad.
    fail_gpio = 1; level = -1;
    assert(bsp_button_prepare_deep_sleep(&level) == ESP_FAIL);
    assert(level == 0);
    fail_gpio = 0;

    assert(bsp_button_prepare_deep_sleep(NULL) == ESP_OK);   // level is optional

    // A teardown failure must be reported and must NOT reconfigure the pin: leaving
    // a live driver or ADC unit behind while the caller heads toward deep sleep would
    // be silent corruption, so the hand-off refuses to proceed.
    assert(bsp_button_init(event_cb, &events) == ESP_OK);    // re-arm live drivers
    gpio_direction_calls = gpio_pull_calls = gpio_level_calls = 0;
    fail_delete = 1; level = -1;
    assert(bsp_button_prepare_deep_sleep(&level) != ESP_OK);
    assert(level == 0);
    assert(gpio_direction_calls == 0 && gpio_pull_calls == 0 && gpio_level_calls == 0);
    fail_delete = 0;
    assert(button_cleanup() == ESP_OK);                     // now clean up for real
    assert_clean();

    assert(bsp_button_init(event_cb, &events) == ESP_OK);    // the BSP stays reusable
}
static void retry_success(void) {
    assert_clean(); reset_faults();
    assert(bsp_button_init(event_cb, &events) == ESP_OK);
    assert(create_calls == BSP_BTN_COUNT && live_buttons == BSP_BTN_COUNT);
    assert(bsp_button_init(event_cb, &events) == ESP_OK);
    assert(create_calls == BSP_BTN_COUNT);
    (void)button_cleanup(); assert_clean();
}
static void check_voltage(int mv, int expected) {
    raw_mv = mv; clock_us += 2000;
    const int before = reads;
    for (int i = 0; i < BSP_BTN_COUNT; ++i) {
        assert(s_drivers[i].base.get_key_level(&s_drivers[i].base) == (i == expected));
    }
    assert(reads - before == CONFIG_ADC_BUTTON_SAMPLE_TIMES);
}
int main(void) {
    for (int i = 1; i <= BSP_BTN_COUNT; ++i) {
        reset_faults(); fail_create = i;
        assert(bsp_button_init(event_cb, &events) != ESP_OK); retry_success();
    }
    for (int i = 1; i <= BSP_BTN_COUNT * 5; ++i) {
        reset_faults(); fail_callback = i;
        assert(bsp_button_init(event_cb, &events) != ESP_OK); retry_success();
    }
    reset_faults(); fail_adc = 1;
    assert(bsp_button_init(event_cb, &events) != ESP_OK); retry_success();
    fail_channel = 1;
    assert(bsp_button_init(event_cb, &events) != ESP_OK); retry_success();
    fail_cal = 1;
    assert(bsp_button_init(event_cb, &events) != ESP_OK); retry_success();
    assert(events == 0);
    assert(bsp_button_init(event_cb, &events) == ESP_OK);
    cb_click(NULL, (void *)(intptr_t)BSP_BTN_OK); assert(events == 1);
    check_voltage(0, BSP_BTN_UP); check_voltage(149, BSP_BTN_UP);
    check_voltage(150, BSP_BTN_DOWN); check_voltage(446, BSP_BTN_DOWN);
    check_voltage(447, BSP_BTN_OK); check_voltage(1899, BSP_BTN_OK);
    check_voltage(1900, -1); check_voltage(3300, -1);
    assert(bsp_button_read_mv() == 3300);
    fail_read = 1; clock_us += 2000;
    for (int i = 0; i < BSP_BTN_COUNT; ++i) assert(!button_level(&s_drivers[i].base));
    assert(bsp_button_read_mv() == -1);
    fail_read = 0; fail_convert = 1; clock_us += 2000;
    for (int i = 0; i < BSP_BTN_COUNT; ++i) assert(!button_level(&s_drivers[i].base));
    assert(bsp_button_read_mv() == -1);
    fail_delete = 1; assert(button_cleanup() != ESP_OK);
    assert(adc_live && cal_live && live_buttons == BSP_BTN_COUNT);
    assert(bsp_button_init(event_cb, &events) == ESP_ERR_INVALID_STATE);
    fail_delete = 0; assert(button_cleanup() == ESP_OK); retry_success();
    check_deep_sleep_prepare();
    for (unsigned cycle=0;cycle<64;++cycle) {
        polling=false; /* The actual component stops its timer before arming. */
        for (int i=BSP_BTN_COUNT-1;i>=0;--i)
            assert(key_enter_power_save(&s_drivers[i].base)==ESP_OK);
        assert(bsp_button_idle_wakeup_armed() && digital_input && wake_enabled && irq_enabled && !polling);
        wake_isr(NULL);
        assert(polling && !irq_enabled && !bsp_button_idle_wakeup_armed());
        assert(key_exit_power_save(&s_drivers[2].base)==ESP_OK);
        assert(!digital_input && !wake_enabled);
        check_voltage(cycle%3==0?0:cycle%3==1?300:595,cycle%3);
    }
    fail_wake=1;polling=false;
    assert(key_enter_power_save(&s_drivers[0].base)!=ESP_OK);
    assert(polling && !digital_input && !wake_enabled && !bsp_button_idle_wakeup_armed());
    fail_wake=0;assert(button_cleanup()==ESP_OK);
    assert(!wake_isr && !wake_enabled && !irq_enabled);
    fail_isr=1;assert(bsp_button_init(event_cb,&events)==ESP_OK);
    for (int i=0;i<BSP_BTN_COUNT;++i) assert(!s_drivers[i].base.enable_power_save);
    assert(button_cleanup()==ESP_OK);fail_isr=0;
    puts("BSP button fault-injection tests: PASS");
}
