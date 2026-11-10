#include "prefs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    vibe_prefs_t old=vibe_prefs_default(),next=old;
    vibe_appearance_t theme=vibe_appearance_default(),other=theme;
    for (unsigned v=0;v<=100;v++) {
        next.volume=v;assert(vibe_prefs_volume_only(&old,&next,&theme,&other));
    }
    next.volume=101;assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));
    next=old;next.volume=25;
#define BLOCK(field,value) do { next.field=(value);assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));next.field=old.field; } while (0)
    BLOCK(brightness,60);BLOCK(screen_seconds,30);BLOCK(focus_minutes,30);
    BLOCK(break_minutes,10);BLOCK(mic_gain,42);BLOCK(latitude,31.2);BLOCK(longitude,121.5);
    BLOCK(weather_enabled,false);
#undef BLOCK
    strcpy(next.city,"上海");assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));strcpy(next.city,old.city);
    strcpy(next.radio_name,"Other");assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));strcpy(next.radio_name,old.radio_name);
    strcpy(next.radio_url,"http://other/radio");assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));strcpy(next.radio_url,old.radio_url);
    other.mode=V_THEME_LIGHT;assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));other=theme;
    other.light_start++;assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));other=theme;
    other.dark_start++;assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));other=theme;
    other.tz_minutes=0;assert(!vibe_prefs_volume_only(&old,&next,&theme,&other));other=theme;
    assert(vibe_prefs_volume_only(&old,&next,&theme,&other));
    puts("Live volume: 0..100, unchanged full snapshots, invalid range and mixed unsafe changes PASS");
}
