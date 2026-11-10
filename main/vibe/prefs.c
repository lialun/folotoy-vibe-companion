#include "prefs.h"
#include <math.h>
#include <string.h>
bool vibe_prefs_volume_only(const vibe_prefs_t *a,const vibe_prefs_t *b,
                            const vibe_appearance_t *old_theme,const vibe_appearance_t *new_theme) {
    return b->volume<=100 && a->brightness==b->brightness && a->screen_seconds==b->screen_seconds &&
        a->focus_minutes==b->focus_minutes && a->break_minutes==b->break_minutes &&
        a->mic_gain==b->mic_gain && a->latitude==b->latitude && a->longitude==b->longitude &&
        a->weather_enabled==b->weather_enabled && !strcmp(a->city,b->city) &&
        !strcmp(a->radio_url,b->radio_url) && !strcmp(a->radio_name,b->radio_name) &&
        old_theme->mode==new_theme->mode && old_theme->light_start==new_theme->light_start &&
        old_theme->dark_start==new_theme->dark_start && old_theme->tz_minutes==new_theme->tz_minutes;
}
vibe_prefs_t vibe_prefs_default(void) {
    return (vibe_prefs_t){.brightness=50,.volume=80,.screen_seconds=15,
        .focus_minutes=25,.break_minutes=5,.mic_gain=30,
        .weather_enabled=true,.city="北京",.latitude=39.9,.longitude=116.4,
        .radio_name="经济之声",.radio_url="http://ngcdn002.cnr.cn/live/jjzs/index.m3u8"};
}
void vibe_prefs_require_weather(vibe_prefs_t *prefs) {
    prefs->weather_enabled=true;
    if (!prefs->city[0]) {
        vibe_prefs_t defaults=vibe_prefs_default();
        strcpy(prefs->city,defaults.city);prefs->latitude=defaults.latitude;prefs->longitude=defaults.longitude;
    }
}
bool vibe_prefs_decode(const void *bytes,size_t size,bool legacy,vibe_prefs_t *prefs,vibe_appearance_t *appearance) {
    vibe_saved_prefs_t stored={0};
    if (!bytes || !prefs || !appearance) return false;
    if (legacy) {
        if (size!=sizeof(vibe_prefs_t)) return false;
        memcpy(&stored.prefs,bytes,size);stored.appearance=vibe_appearance_default();
    } else {
        if (size!=sizeof(stored)) return false;
        memcpy(&stored,bytes,size);
        if (stored.version!=VIBE_PREFS_VERSION || !vibe_appearance_valid(&stored.appearance)) return false;
    }
    vibe_prefs_t *p=&stored.prefs;
    if (p->brightness<10 || p->brightness>100 || p->volume>100 || p->mic_gain>42 ||
        p->screen_seconds<3 || p->screen_seconds>300 || p->focus_minutes<1 || p->focus_minutes>180 ||
        p->break_minutes<1 || p->break_minutes>60 || !isfinite(p->latitude) || fabs(p->latitude)>90 ||
        !isfinite(p->longitude) || fabs(p->longitude)>180) return false;
    p->city[sizeof(p->city)-1]=0;p->radio_url[sizeof(p->radio_url)-1]=0;p->radio_name[sizeof(p->radio_name)-1]=0;
    if (p->screen_seconds>60) p->screen_seconds=60;
    vibe_prefs_require_weather(p);
    *prefs=*p;*appearance=stored.appearance;return true;
}
