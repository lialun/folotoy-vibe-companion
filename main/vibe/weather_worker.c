#include "vibe.h"
#include "weather_transport.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* v_mutex protects lifecycle as well as model/status. The C3 has one core.
 * A finished worker has released all request/JSON resources and only parks;
 * another task deletes it, synchronously reclaiming its 8 KiB stack. */
static TaskHandle_t weather_worker;
static bool weather_finished,weather_paused;
static uint64_t next_check;
static const char *TAG="vibe_weather";
static bool idle_locked(void) {
    return !weather_paused && v_status.wifi && !v_status.provisioning &&
        !v_status.audio_busy && !v_model.recording && !v_model.radio_requested;
}
static bool weather_idle(void) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    bool allowed=idle_locked();
    xSemaphoreGive(v_mutex);
    return allowed;
}
static bool due(uint64_t now,uint64_t updated,uint64_t attempted) {
    return (!updated || now-updated>=900000) && (!attempted || now-attempted>=60000);
}
static bool same_city(const vibe_prefs_t *p) {
    return v_prefs.weather_enabled && p->latitude==v_prefs.latitude &&
        p->longitude==v_prefs.longitude && !strcmp(p->city,v_prefs.city);
}
static void weather_task(void *arg) {
    (void)arg;
    vibe_status_t s;vibe_prefs_t p;vibe_snapshot(NULL,&p,&s);
    const uint64_t now=vibe_now();
    if (p.weather_enabled && weather_idle()) {
        char url[896];char *body;
        if (due(now,s.weather_at_ms,s.weather_attempt_ms)) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.weather_attempt_ms=now;xSemaphoreGive(v_mutex);
            snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,is_day"
                "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max,wind_speed_10m_max"
                "&hourly=temperature_2m,precipitation_probability,precipitation&forecast_days=3&forecast_hours=12"
                "&timezone=auto&timeformat=unixtime&wind_speed_unit=ms",p.latitude,p.longitude);
            body=vibe_weather_fetch(url,weather_idle,vibe_now);vibe_weather_t weather;
            if (body && weather_idle() && vibe_weather_parse(body,&weather)) {
                xSemaphoreTake(v_mutex,portMAX_DELAY);
                bool accepted=same_city(&p);
                if (accepted) { v_status.forecast=weather;v_status.weather_at_ms=vibe_now(); }
                xSemaphoreGive(v_mutex);
                if (accepted) ESP_LOGI(TAG,"Weather cache updated: hourly samples=%u",weather.hour_count);
            }
            free(body);
        }
        if (due(now,s.air_at_ms,s.air_attempt_ms) && weather_idle()) {
            xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.air_attempt_ms=now;xSemaphoreGive(v_mutex);
            snprintf(url,sizeof(url),"https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=pm2_5,us_aqi&forecast_days=1&timezone=auto",p.latitude,p.longitude);
            body=vibe_weather_fetch(url,weather_idle,vibe_now);vibe_air_t air;
            if (body && weather_idle() && vibe_air_parse(body,&air)) {
                xSemaphoreTake(v_mutex,portMAX_DELAY);
                bool accepted=same_city(&p);
                if (accepted) { v_status.air=air;v_status.air_at_ms=vibe_now(); }
                xSemaphoreGive(v_mutex);
                if (accepted) ESP_LOGI(TAG,"Air cache updated: PM2.5=%s, AQI=%s",air.pm25_valid?"available":"missing",air.aqi_valid?"available":"missing");
            }
            free(body);
        }
    }
    xSemaphoreTake(v_mutex,portMAX_DELAY);weather_finished=true;xSemaphoreGive(v_mutex);
    for (;;) vTaskSuspend(NULL);
}
static void reap_locked(void) {
    if (weather_worker && weather_finished) {
        vTaskDelete(weather_worker);weather_worker=NULL;weather_finished=false;
        ESP_LOGI(TAG,"Weather worker released");
    }
}
void vibe_weather_poll(uint64_t now) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    reap_locked();
    if (!weather_worker && idle_locked() && now>=next_check) {
        next_check=now+5000;
        if (v_prefs.weather_enabled &&
            (due(now,v_status.weather_at_ms,v_status.weather_attempt_ms) ||
             due(now,v_status.air_at_ms,v_status.air_attempt_ms))) {
            weather_finished=false;
            if (xTaskCreate(weather_task,"vibe_weather",8192,NULL,2,&weather_worker)!=pdPASS) {
                weather_worker=NULL;ESP_LOGW(TAG,"Weather deferred: no worker memory");
            }
        }
    }
    xSemaphoreGive(v_mutex);
}
void vibe_weather_pause(void) {
    /* No allocation here. Setting paused under the same lock as task creation
     * prevents a new request slipping in between cancellation and audio start. */
    for (;;) {
        xSemaphoreTake(v_mutex,portMAX_DELAY);weather_paused=true;reap_locked();
        bool released=!weather_worker;
        xSemaphoreGive(v_mutex);
        if (released) return;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void vibe_weather_resume(void) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);weather_paused=false;xSemaphoreGive(v_mutex);
}
