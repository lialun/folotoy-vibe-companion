#include "signal.h"
void vibe_signal_update(vibe_signal_t *s,int rssi) {
    if (!s || rssi>=0 || rssi< -127) return;
    if (!s->valid) {
        s->filtered=rssi*256;s->valid=true;s->bars=rssi>=-60?3:rssi>=-75?2:1;return;
    }
    s->filtered=(s->filtered+rssi*256)/2;
    if (s->bars==3 && s->filtered<=-63*256) s->bars=2;
    if (s->bars==2) {
        if (s->filtered>=-57*256) s->bars=3;
        else if (s->filtered<=-78*256) s->bars=1;
    }
    if (s->bars==1 && s->filtered>=-72*256) s->bars=s->filtered>=-57*256?3:2;
}
