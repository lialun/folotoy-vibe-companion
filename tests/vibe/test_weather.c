#include "weather.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char fixture[]="{\"current\":{\"temperature_2m\":-2.6,\"apparent_temperature\":-5.4,\"relative_humidity_2m\":73,\"wind_speed_10m\":8.6,\"wind_direction_10m\":315,\"weather_code\":85},\"daily\":{\"temperature_2m_min\":[-5,-2,1],\"temperature_2m_max\":[2,5,8],\"weather_code\":[85,0,61],\"precipitation_probability_max\":[80,10,70]}}";
int main(void) {
    vibe_weather_t w={0}; assert(vibe_weather_parse(fixture,&w));
    assert(w.valid && w.temperature==-3 && w.feels==-5 && !w.wind_valid && w.humidity==73);
    assert(w.days[2].low==1 && w.days[2].high==8 && w.days[2].rain==70);
    assert(!strcmp(vibe_weather_text(85),"小阵雪")); assert(!strcmp(vibe_weather_text(95),"雷暴"));
    const int codes[]={0,1,2,3,45,48,51,53,55,56,57,61,63,65,66,67,71,73,75,77,80,81,82,85,86,95,96,97,99};
    for (unsigned i=0;i<sizeof(codes)/sizeof(codes[0]);i++) {
        assert(vibe_weather_icon(codes[i],true)!=VW_UNKNOWN && vibe_weather_icon(codes[i],false)!=VW_UNKNOWN);
        assert(strcmp(vibe_weather_text(codes[i]),"暂无天气"));
    }
    assert(vibe_weather_icon(0,false)==VW_MOON && vibe_weather_icon(2,false)==VW_CLOUD_MOON);
    assert(vibe_weather_icon(100,true)==VW_UNKNOWN);
    vibe_air_t air={0};assert(vibe_air_parse("{\"current\":{\"pm2_5\":18.15},\"current_units\":{\"pm2_5\":\"μg/m³\"}}",&air));
    assert(air.valid && air.pm25_tenths==182);
    const char *invalid[]={"{}","{\"current\":{\"pm2_5\":2}}","{\"current\":{\"pm2_5\":null},\"current_units\":{\"pm2_5\":\"ug/m3\"}}","{\"current\":{\"pm2_5\":2},\"current_units\":{\"pm2_5\":\"mg/m3\"}}","{\"current\":{\"pm2_5\":-1},\"current_units\":{\"pm2_5\":\"ug/m3\"}}"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) { assert(!vibe_air_parse(invalid[i],&air));assert(air.pm25_tenths==182); }
    assert(!strcmp(vibe_weather_direction(359),"北风")); assert(!strcmp(vibe_weather_direction(315),"西北风"));
    for (unsigned i=0;i<4;i++) {
        cJSON *root=cJSON_Parse(fixture);cJSON *current=cJSON_GetObjectItemCaseSensitive(root,"current");
        cJSON_AddNumberToObject(current,"is_day",i==0?0:i==1?1:i==2?0.5:2);
        char *input=cJSON_PrintUnformatted(root);
        if (i<2) { assert(vibe_weather_parse(input,&w));assert(w.is_day==(i==1)); }
        else assert(!vibe_weather_parse(input,&w));
        cJSON_free(input);cJSON_Delete(root);
    }
    vibe_weather_t previous=w;
    const char *keys[]={"temperature_2m","relative_humidity_2m","wind_direction_10m","weather_code"};
    for (unsigned i=0;i<4;i++) {
        cJSON *root=cJSON_Parse(fixture);cJSON *current=cJSON_GetObjectItemCaseSensitive(root,"current");
        cJSON_ReplaceItemInObjectCaseSensitive(current,keys[i],cJSON_CreateNull());
        char *bad=cJSON_PrintUnformatted(root); assert(!vibe_weather_parse(bad,&w)); assert(!memcmp(&w,&previous,sizeof(w)));cJSON_free(bad);cJSON_Delete(root);
    }
    cJSON *root=cJSON_Parse(fixture);cJSON *daily=cJSON_GetObjectItemCaseSensitive(root,"daily");
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(daily,"temperature_2m_max"),2);
    char *bad=cJSON_PrintUnformatted(root);assert(!vibe_weather_parse(bad,&w));cJSON_free(bad);cJSON_Delete(root);
    assert(!vibe_weather_parse("{}",&w) && !vibe_weather_parse("{",&w) && !memcmp(&w,&previous,sizeof(w)));
    const struct { float ms;unsigned level; } winds[]={{0,0},{.29f,0},{.3f,1},{1.59f,1},{1.6f,2},{3.39f,2},{3.4f,3},{5.5f,4},{8.f,5},{13.9f,7},{51.f,16},{56.1f,17},{100.f,17}};
    for (unsigned i=0;i<sizeof(winds)/sizeof(winds[0]);i++) assert(vibe_weather_wind_level(winds[i].ms)==winds[i].level);
    assert(!strcmp(vibe_air_text(50),"良好") && !strcmp(vibe_air_text(51),"中等") && !strcmp(vibe_air_text(151),"不健康"));
    const struct { unsigned tenths;vibe_uv_level_t level; } uv[]={
        {0,V_UV_LOW},{23,V_UV_LOW},{24,V_UV_LOW},{25,V_UV_MODERATE},
        {54,V_UV_MODERATE},{55,V_UV_HIGH},{74,V_UV_HIGH},{75,V_UV_VERY_HIGH},
        {104,V_UV_VERY_HIGH},{105,V_UV_EXTREME},{110,V_UV_EXTREME},{500,V_UV_EXTREME}};
    for (unsigned i=0;i<sizeof(uv)/sizeof(uv[0]);i++) assert(vibe_weather_uv_level(uv[i].tenths)==uv[i].level);
    assert(!strcmp(vibe_weather_uv_text(vibe_weather_uv_level(23)),"低"));
    assert(!strcmp(vibe_weather_uv_text(V_UV_EXTREME),"极高"));
    assert(vibe_air_parse("{\"current\":{\"us_aqi\":99},\"current_units\":{\"us_aqi\":\"USAQI\"}}",&air));
    assert(air.aqi_valid && air.aqi==99 && !air.pm25_valid);
    assert(!vibe_air_parse("{\"current\":{\"us_aqi\":99},\"current_units\":{\"us_aqi\":\"EAQI\"}}",&air));
    FILE *f=fopen("tests/vibe/fixtures/weather.json","rb");assert(f);char data[4096];size_t len=fread(data,1,sizeof(data)-1,f);fclose(f);data[len]=0;
    assert(vibe_weather_parse(data,&w) && w.hour_count==12 && w.wind_valid && w.gust_valid && w.days[0].uv_valid && w.days[2].wind_valid);
    assert(w.tz_seconds==28800 && w.hours[0].fields==7);
    assert(vibe_weather_next_hour(&w,w.hours[0].at)==1);
    assert(vibe_weather_next_hour(&w,w.hours[1].at-1)==1);
    assert(vibe_weather_next_hour(&w,w.hours[11].at)==12);
    root=cJSON_Parse(data);cJSON *hourly=cJSON_GetObjectItemCaseSensitive(root,"hourly");
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(hourly,"precipitation"),1,cJSON_CreateNull());
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(hourly,"precipitation_probability"),2,cJSON_CreateNumber(101));
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(hourly,"temperature_2m"),3,cJSON_CreateNull());
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"daily"),"uv_index_max"),0,cJSON_CreateNull());
    char *partial=cJSON_PrintUnformatted(root);assert(vibe_weather_parse(partial,&w));
    assert(w.hours[1].fields==3 && w.hours[2].fields==5 && w.hours[3].fields==6 && !w.days[0].uv_valid);
    cJSON_free(partial);cJSON_Delete(root);
    int temperatures[6]={-12,-11,-10,-10,-11,-12},y[6];bool valid[6]={true,true,true,true,true,true};
    vibe_weather_plot(temperatures,valid,y);assert(y[0]>y[2] && y[2]==y[3]);
    for (unsigned i=0;i<6;i++) assert(y[i]>=147 && y[i]<=191);
    for (unsigned i=0;i<6;i++) temperatures[i]=5;
    vibe_weather_plot(temperatures,valid,y);for (unsigned i=0;i<6;i++) assert(y[i]==169);
    puts("Weather: hourly/daily data, AQI/PM2.5, wind levels, missing values, retained sample and plot bounds PASS");
}
