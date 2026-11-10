#include "appearance.h"
vibe_appearance_t vibe_appearance_default(void) {
    return (vibe_appearance_t){V_THEME_SCHEDULED,8*60,21*60,480};
}
bool vibe_appearance_valid(const vibe_appearance_t *a) {
    return a && a->mode<=V_THEME_SCHEDULED && a->light_start<1440 && a->dark_start<1440 &&
        a->light_start!=a->dark_start && a->tz_minutes>=-720 && a->tz_minutes<=840;
}
bool vibe_appearance_light(const vibe_appearance_t *a,int64_t epoch,int tz_minutes) {
    if (!vibe_appearance_valid(a)) return false;
    if (a->mode!=V_THEME_SCHEDULED) return a->mode==V_THEME_LIGHT;
    if (epoch<1700000000 || tz_minutes< -720 || tz_minutes>840) return false;
    unsigned minute=(unsigned)(((epoch/60+tz_minutes)%1440+1440)%1440);
    if (a->light_start<a->dark_start) return minute>=a->light_start && minute<a->dark_start;
    return minute>=a->light_start || minute<a->dark_start;
}
