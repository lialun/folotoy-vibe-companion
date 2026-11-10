#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int low, high, code, rain;
    bool uv_valid, wind_valid;
    unsigned uv_tenths;
    float wind_ms;
} vibe_forecast_t;
#define VIBE_WEATHER_HOURS 12
enum { V_HOUR_TEMP=1, V_HOUR_PROB=2, V_HOUR_RAIN=4 };
typedef struct {
    int64_t at;
    int temperature, probability;
    unsigned rain_tenths, fields;
} vibe_hour_t;
typedef struct {
    bool valid, is_day;
    int temperature, feels, humidity, direction, code;
    bool wind_valid, gust_valid;
    float wind_ms, gust_ms;
    int tz_seconds;
    int64_t sampled_at;
    vibe_forecast_t days[3];
    vibe_hour_t hours[VIBE_WEATHER_HOURS];
    unsigned hour_count;
} vibe_weather_t;
typedef struct { bool valid, pm25_valid, aqi_valid; unsigned pm25_tenths, aqi; } vibe_air_t;
typedef enum { VW_UNKNOWN, VW_SUN, VW_MOON, VW_CLOUD_SUN, VW_CLOUD_MOON, VW_CLOUD,
    VW_FOG, VW_DRIZZLE, VW_RAIN, VW_SNOW, VW_THUNDER, VW_HAIL } vibe_weather_icon_t;
/* Accept a complete bounded Open-Meteo response, retaining the old sample on failure. */
bool vibe_weather_parse(const char *json, vibe_weather_t *out);
bool vibe_air_parse(const char *json, vibe_air_t *out);
vibe_weather_icon_t vibe_weather_icon(int code,bool day);
const char *vibe_weather_text(int code);
const char *vibe_weather_direction(int degrees);
unsigned vibe_weather_wind_level(float metres_per_second);
const char *vibe_air_text(unsigned us_aqi);
typedef enum { V_UV_LOW, V_UV_MODERATE, V_UV_HIGH, V_UV_VERY_HIGH, V_UV_EXTREME } vibe_uv_level_t;
/* Classify the nearest whole UVI, retaining tenths separately for display. */
vibe_uv_level_t vibe_weather_uv_level(unsigned uv_tenths);
const char *vibe_weather_uv_text(vibe_uv_level_t level);
/* Index of the next full forecast hour; may equal hour_count when exhausted. */
unsigned vibe_weather_next_hour(const vibe_weather_t *weather,int64_t now);
void vibe_weather_plot(const int temperatures[6],const bool valid[6],int y[6]);
#ifdef __cplusplus
}
#endif
