#include "appearance.h"
#include "prefs.h"
#include "signal.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int64_t at(unsigned minute) { return 1700006400LL+(int64_t)minute*60; } /* UTC midnight */
int main(void) {
    vibe_appearance_t a=vibe_appearance_default();assert(vibe_appearance_valid(&a));
    assert(a.mode==V_THEME_SCHEDULED && a.light_start==480 && a.dark_start==1260);
    assert(!vibe_appearance_light(&a,0,480));a.mode=V_THEME_LIGHT;assert(vibe_appearance_light(&a,0,480));
    a.mode=V_THEME_SCHEDULED;assert(!vibe_appearance_light(&a,0,480));
    assert(!vibe_appearance_light(&a,at(479),0));assert(vibe_appearance_light(&a,at(480),0));
    assert(vibe_appearance_light(&a,at(1259),0));assert(!vibe_appearance_light(&a,at(1260),0));
    assert(vibe_appearance_light(&a,at(0),480));assert(!vibe_appearance_light(&a,at(0),-60));
    a.light_start=1140;a.dark_start=420;
    assert(vibe_appearance_light(&a,at(0),0));assert(!vibe_appearance_light(&a,at(420),0));
    assert(vibe_appearance_light(&a,at(1140),0));assert(!vibe_appearance_light(&a,at(1139),0));
    a.light_start=a.dark_start;assert(!vibe_appearance_valid(&a));
    a=vibe_appearance_default();a.mode=3;assert(!vibe_appearance_valid(&a));
    a=vibe_appearance_default();a.dark_start=1440;assert(!vibe_appearance_valid(&a));
    a=vibe_appearance_default();a.tz_minutes=841;assert(!vibe_appearance_valid(&a));
    vibe_prefs_t original=vibe_prefs_default(),decoded={0};original.brightness=67;original.focus_minutes=42;
    vibe_prefs_t defaults=vibe_prefs_default();
    assert(defaults.brightness==50 && defaults.volume==80 && defaults.screen_seconds==15);
    assert(defaults.weather_enabled && !strcmp(defaults.city,"北京") && defaults.latitude==39.9 && defaults.longitude==116.4);
    strcpy(original.city,"北京");original.latitude=39.9;original.longitude=116.4;original.weather_enabled=true;
    original.screen_seconds=180;
    assert(vibe_prefs_decode(&original,sizeof(original),true,&decoded,&a));
    assert(decoded.brightness==67 && decoded.focus_minutes==42 && decoded.screen_seconds==60 && decoded.weather_enabled);
    assert(!strcmp(decoded.city,"北京") && !strcmp(decoded.radio_url,original.radio_url) && a.mode==V_THEME_SCHEDULED);
    original.weather_enabled=false;original.city[0]=0;original.latitude=0;original.longitude=0;
    assert(vibe_prefs_decode(&original,sizeof(original),true,&decoded,&a));
    assert(decoded.weather_enabled && !strcmp(decoded.city,"北京") && decoded.latitude==39.9 && decoded.brightness==67);
    strcpy(original.city,"上海");original.latitude=31.2;original.longitude=121.5;
    assert(vibe_prefs_decode(&original,sizeof(original),true,&decoded,&a));
    assert(decoded.weather_enabled && !strcmp(decoded.city,"上海") && decoded.latitude==31.2 && decoded.longitude==121.5);
    vibe_saved_prefs_t stored={.version=VIBE_PREFS_VERSION,.prefs=decoded,.appearance=a};stored.appearance.mode=V_THEME_SCHEDULED;stored.appearance.light_start=480;
    assert(vibe_prefs_decode(&stored,sizeof(stored),false,&decoded,&a) && a.light_start==480 && a.mode==V_THEME_SCHEDULED);
    assert(!vibe_prefs_decode(&stored,sizeof(stored)-1,false,&decoded,&a));
    stored.version++;assert(!vibe_prefs_decode(&stored,sizeof(stored),false,&decoded,&a));stored.version--;
    stored.appearance.dark_start=480;assert(!vibe_prefs_decode(&stored,sizeof(stored),false,&decoded,&a));
    original.volume=101;assert(!vibe_prefs_decode(&original,sizeof(original),true,&decoded,&a));
    vibe_signal_t signal={0};vibe_signal_update(&signal,0);assert(!signal.valid && signal.bars==0);
    vibe_signal_update(&signal,-128);assert(!signal.valid);
    vibe_signal_update(&signal,-59);assert(signal.valid && signal.bars==3);
    for (int i=0;i<20;i++) { vibe_signal_update(&signal,i%2?-59:-61);assert(signal.bars==3); }
    for (int i=0;i<8;i++) {
        vibe_signal_update(&signal,-68);
    }
    assert(signal.bars==2);
    for (int i=0;i<20;i++) { vibe_signal_update(&signal,i%2?-74:-76);assert(signal.bars==2); }
    for (int i=0;i<8;i++) {
        vibe_signal_update(&signal,-85);
    }
    assert(signal.bars==1);
    for (int i=0;i<8;i++) {
        vibe_signal_update(&signal,-40);
    }
    assert(signal.bars==3);
    signal=(vibe_signal_t){0};assert(!signal.valid && signal.bars==0);vibe_signal_update(&signal,-90);assert(signal.bars==1);
    puts("Theme boundaries, timezone/midnight, legacy prefs migration and RSSI hysteresis: PASS");
}
