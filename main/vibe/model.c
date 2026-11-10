#include "model.h"
#include <string.h>

void vibe_model_init(vibe_model_t *m, uint64_t now) {
    memset(m, 0, sizeof(*m));
    m->screen_on=true; m->focus_minutes=25; m->break_minutes=5;
    m->brightness=50; m->volume=80; m->screen_seconds=15;
    m->theme_mode=V_THEME_SCHEDULED;
    m->radio_count=6;
    m->last_activity_ms=now; m->remaining_ms=25*60000ULL;
}
uint64_t vibe_model_remaining(const vibe_model_t *m, uint64_t now) {
    if (m->timer==V_TIMER_READY || m->timer==V_TIMER_ARMED) return m->focus_minutes*60000ULL;
    if (m->timer==V_TIMER_FINISHED) return (m->timer_break?m->break_minutes:m->focus_minutes)*60000ULL;
    return m->timer == V_TIMER_RUNNING ? (m->deadline_ms > now ? m->deadline_ms-now : 0) : m->remaining_ms;
}
bool vibe_model_completion_visible(const vibe_model_t *m, uint64_t now) {
    return m->completion_until_ms && now < m->completion_until_ms;
}
static void start_phase(vibe_model_t *m,uint64_t now) {
    if (m->timer!=V_TIMER_PAUSED) {
        m->phase_minutes=m->timer_break ? m->break_minutes : m->focus_minutes;
        m->remaining_ms=m->phase_minutes*60000ULL;
    }
    m->deadline_ms=now+m->remaining_ms; m->timer=V_TIMER_RUNNING;
    m->completion_until_ms=0; m->alarm_pending=false;
}
static void reset_weather(vibe_model_t *m) {
    m->weather_page=0;
}
static void exit_timer(vibe_model_t *m) {
    m->timer=V_TIMER_READY;m->timer_break=false;m->phase_minutes=0;m->deadline_ms=0;
    m->remaining_ms=m->focus_minutes*60000ULL;m->completion_until_ms=0;m->alarm_pending=false;
    m->page=V_HOME;m->reset_confirm=false;
    reset_weather(m);
}
unsigned vibe_model_wait_ms(const vibe_model_t *m, uint64_t now) {
    if (m->screen_on || m->recording) return 200;
    if (m->timer==V_TIMER_RUNNING) {
        if (now>=m->deadline_ms) return 1;
        uint64_t left=m->deadline_ms-now;
        return left<30000 ? (unsigned)left : 30000;
    }
    return 30000;
}
unsigned vibe_model_tick(vibe_model_t *m, uint64_t now) {
    unsigned actions=0;
    if (m->timer == V_TIMER_RUNNING && now >= m->deadline_ms) {
        m->timer=V_TIMER_FINISHED;
        m->completion_until_ms=now+10000;
        if (!m->timer_break) ++m->completed;
        m->timer_break=!m->timer_break;
        m->remaining_ms=(m->timer_break?m->break_minutes:m->focus_minutes)*60000ULL;
        m->alarm_pending=true;
        m->screen_on=true; m->last_activity_ms=now;
    }
    if (m->alarm_pending && !m->recording && m->page != V_PROVISION) {
        m->alarm_pending=false; actions|=V_ALARM;
    }
    if (!m->recording && !vibe_model_completion_visible(m,now) && m->screen_seconds && now-m->last_activity_ms >= m->screen_seconds*1000ULL)
        m->screen_on=false;
    return actions;
}
unsigned vibe_model_key(vibe_model_t *m, vibe_key_t key, vibe_event_t event, uint64_t now) {
    if (key > V_SETTINGS) return 0;
    unsigned actions=0;
    if (event == V_PRESS) m->long_seen[key]=false;
    if (event == V_LONG) {
        /* One hold performs one action even if a repeated LONG is delivered. */
        if (m->long_seen[key]) return 0;
        m->long_seen[key]=true;
    }
    if (event == V_RELEASE) {
        if (key == V_UP && m->recording) { m->recording=false;m->last_activity_ms=now; actions|=V_STOP_AUDIO; }
        return actions;
    }
    if (m->wake_only[key] && event != V_PRESS) {
        /* One deliberate hold may wake and enter the timer; a tap only wakes. */
        if ((key==V_DOWN || key==V_SETTINGS) && event==V_LONG && m->page==V_HOME) m->wake_only[key]=false;
        else {
            if (event == V_CLICK) m->wake_only[key]=false;
            return 0;
        }
    }
    if (event == V_CLICK && m->suppress_click[key]) { m->suppress_click[key]=false; return 0; }
    if (event == V_PRESS) { m->suppress_click[key]=false; m->wake_only[key]=false; }
    if (!m->screen_on) {
        m->screen_on=true; m->last_activity_ms=now;
        reset_weather(m);
        if (!(m->page==V_HOME && event==V_PRESS && (key==V_UP || (key==V_SETTINGS && (m->timer!=V_TIMER_READY || m->radio_active))))) {
            m->suppress_click[key]=true; m->wake_only[key]=true; return 0;
        }
    }
    m->last_activity_ms=now;
    if (event == V_LONG) m->suppress_click[key]=true;
    if (m->page == V_HOME) {
        if (key == V_UP && event == V_PRESS && m->voice_ready && !m->recording) {
            m->recording=true; m->recording_since_ms=now; return V_START_AUDIO;
        }
        if (m->recording) return 0;
        if (key==V_SETTINGS && event==V_CLICK) {
            if (m->radio_active) {
                if (m->radio_count) { m->radio_index=(m->radio_index+1)%m->radio_count;m->radio_requested=true;return V_RADIO_STATION; }
                return 0;
            }
            if (m->timer==V_TIMER_RUNNING) { m->remaining_ms=vibe_model_remaining(m,now);m->timer=V_TIMER_PAUSED; }
            else if (m->timer!=V_TIMER_READY) start_phase(m,now);
            else { m->weather_page=(m->weather_page+1)%4; }
        }
        if (key == V_DOWN && event == V_CLICK && m->enter_ready) return V_ENTER;
        if (key == V_DOWN && event == V_LONG) {
            if (m->radio_active) { m->page=V_RADIO_EXIT;m->reset_confirm=false;return 0; }
            if (m->timer==V_TIMER_READY) {
                m->timer=V_TIMER_ARMED;m->timer_break=false;m->phase_minutes=0;
                m->remaining_ms=m->focus_minutes*60000ULL;m->completion_until_ms=0;m->alarm_pending=false;
            } else if (m->timer==V_TIMER_ARMED || m->timer==V_TIMER_FINISHED) exit_timer(m);
            else { m->page=V_POMODORO; m->selected=0; m->reset_confirm=false; }
        }
        if (key == V_SETTINGS && event == V_LONG) { m->page=V_FUNCTIONS; m->selected=0; }
    } else if (m->page == V_POMODORO || m->page == V_RADIO_EXIT) {
        if (event != V_CLICK) return 0;
        if (key == V_UP || key == V_DOWN) {
            /* Direct choices, not a menu. The entry hold never generates this click. */
            if (key == V_DOWN) {
                if (m->page==V_RADIO_EXIT) {
                    m->radio_active=false;m->radio_requested=false;m->page=V_HOME;
                    m->reset_confirm=false;reset_weather(m);return V_RADIO_CHANGED;
                }
                bool radio=m->switch_radio;exit_timer(m);m->switch_radio=false;
                if (radio) { m->radio_active=true;m->radio_requested=m->radio_count>0;if (!m->radio_count) m->radio_index=0;return V_RADIO_CHANGED; }
            }
            m->switch_radio=false;
            m->page=V_HOME;m->reset_confirm=false;
        }
    } else if (m->page == V_FUNCTIONS) {
        if (key == V_SETTINGS && event == V_LONG) { m->page=V_HOME; return 0; }
        if (event != V_CLICK) return 0;
        if (key == V_UP && m->selected) --m->selected;
        if (key == V_DOWN && m->selected<1) ++m->selected;
        if (key == V_SETTINGS) {
            if (m->selected==0) { m->page=V_SETTINGS_PAGE; m->selected=0; m->editing=false; }
            else if (m->selected==1) {
                if (m->radio_active) {
                    m->page=V_HOME;
                    if (!m->radio_requested && m->radio_count) { m->radio_requested=true;return V_RADIO_CHANGED; }
                }
                else if (m->timer==V_TIMER_RUNNING || m->timer==V_TIMER_PAUSED) { m->switch_radio=true;m->page=V_POMODORO; }
                else { exit_timer(m);m->radio_active=true;m->radio_requested=m->radio_count>0;if (!m->radio_count) m->radio_index=0;return V_RADIO_CHANGED; }
            }
        }
    } else if (m->page == V_SETTINGS_PAGE) {
        if (key == V_SETTINGS && event == V_LONG) {
            if (m->editing) m->editing=false;
            else { m->page=V_FUNCTIONS; m->selected=0; }
            return 0;
        }
        if (event != V_CLICK) return 0;
        if (!m->editing) {
            if (key == V_UP && m->selected) { --m->selected; m->reset_confirm=false; }
            if (key == V_DOWN && m->selected<V_SETTING_COUNT-1) { ++m->selected; m->reset_confirm=false; }
            if (key == V_SETTINGS) {
                if (m->selected==V_SETTING_WIFI) return V_START_PROVISION;
                if (m->selected==V_SETTING_UNPAIR) {
                    if (!m->reset_confirm) { m->reset_confirm=true; return 0; }
                    m->reset_confirm=false; return V_UNPAIR;
                }
                m->editing=true;
            }
        } else if (key == V_SETTINGS) m->editing=false;
        else {
            if (m->selected==V_SETTING_THEME) {
                m->theme_mode=(vibe_theme_mode_t)((m->theme_mode+(key==V_UP?2:1))%3);
                return V_PREFS_CHANGED;
            }
            int delta=key==V_UP ? 10 : -10;
            unsigned *v=m->selected==V_SETTING_BRIGHTNESS ? &m->brightness : m->selected==V_SETTING_VOLUME ? &m->volume : &m->screen_seconds;
            if (m->selected==V_SETTING_SCREEN_TIMEOUT) delta=key==V_UP ? 1 : -1;
            int n=(int)*v+delta, min=m->selected==V_SETTING_VOLUME ? 0 : m->selected==V_SETTING_SCREEN_TIMEOUT ? 3 : 10, max=m->selected==V_SETTING_SCREEN_TIMEOUT ? 60 : 100;
            *v=(unsigned)(n<min ? min : n>max ? max : n); return V_PREFS_CHANGED;
        }
    }
    return actions;
}
