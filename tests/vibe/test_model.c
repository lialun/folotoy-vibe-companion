#include "model.h"
#include <assert.h>
#include <stdio.h>
static unsigned key(vibe_model_t *m,vibe_key_t k,vibe_event_t e,uint64_t t) { return vibe_model_key(m,k,e,t); }
static unsigned tap(vibe_model_t *m,vibe_key_t k,uint64_t t) {
    unsigned a=key(m,k,V_PRESS,t);a|=key(m,k,V_RELEASE,t+50);return a|key(m,k,V_CLICK,t+50);
}
static unsigned hold(vibe_model_t *m,vibe_key_t k,uint64_t t) {
    unsigned a=key(m,k,V_PRESS,t);a|=key(m,k,V_LONG,t+700);return a|key(m,k,V_RELEASE,t+750);
}
static void voice_and_enter(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.voice_ready=true;m.enter_ready=true;
    assert(key(&m,V_UP,V_PRESS,1)==V_START_AUDIO);
    assert(hold(&m,V_DOWN,5)==0 && m.page==V_HOME);
    assert(tap(&m,V_SETTINGS,800)==0 && m.timer==V_TIMER_READY);
    assert(key(&m,V_UP,V_RELEASE,1000)==V_STOP_AUDIO);
    assert(tap(&m,V_DOWN,1100)==V_ENTER);
    m.voice_ready=false;assert(key(&m,V_UP,V_PRESS,1200)==0);
    assert(tap(&m,V_DOWN,1300)==V_ENTER);m.enter_ready=false;assert(tap(&m,V_DOWN,1400)==0);
    m.voice_ready=true;m.enter_ready=true;m.screen_on=false;
    assert(key(&m,V_UP,V_PRESS,2000)==V_START_AUDIO && m.screen_on);
    assert(key(&m,V_UP,V_RELEASE,2100)==V_STOP_AUDIO);
    m.screen_on=false;assert(tap(&m,V_DOWN,2200)==0 && m.screen_on);
    assert(tap(&m,V_DOWN,2300)==V_ENTER);
}
static void prepare_timer(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.voice_ready=true;m.enter_ready=true;
    for (unsigned i=0;i<3;i++) {
        assert(tap(&m,V_SETTINGS,100*i)==0 && m.timer==V_TIMER_READY);
        assert(!m.deadline_ms && m.page==V_HOME);
    }
    m.screen_on=false;tap(&m,V_SETTINGS,400);
    assert(m.screen_on && m.timer==V_TIMER_READY); /* Wake must not start focus. */
    m.screen_on=false;
    assert(hold(&m,V_DOWN,500)==0 && m.timer==V_TIMER_ARMED && m.page==V_HOME);
    assert(m.screen_on && !m.deadline_ms && vibe_model_remaining(&m,1300)==25*60000ULL);
    assert(key(&m,V_DOWN,V_CLICK,1300)==0); /* No stray Enter after the hold. */
    vibe_model_tick(&m,100000);
    assert(m.timer==V_TIMER_ARMED && vibe_model_remaining(&m,100000)==25*60000ULL);
    assert(key(&m,V_UP,V_PRESS,100001)==V_START_AUDIO);
    tap(&m,V_SETTINGS,100002);assert(m.timer==V_TIMER_ARMED);
    assert(key(&m,V_UP,V_RELEASE,100100)==V_STOP_AUDIO);
    assert(tap(&m,V_DOWN,100200)==V_ENTER);
    hold(&m,V_SETTINGS,100300);assert(m.page==V_FUNCTIONS && m.timer==V_TIMER_ARMED);
    assert(key(&m,V_SETTINGS,V_CLICK,101100)==0 && m.page==V_FUNCTIONS);
    hold(&m,V_SETTINGS,101200);assert(m.page==V_HOME && m.timer==V_TIMER_ARMED);
    tap(&m,V_SETTINGS,102100);assert(m.timer==V_TIMER_RUNNING && m.page==V_HOME);
}
static void timer_controls(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.focus_minutes=1;
    assert(vibe_model_remaining(&m,0)==60000);
    hold(&m,V_DOWN,0);tap(&m,V_SETTINGS,800);
    assert(m.timer==V_TIMER_RUNNING && m.page==V_HOME && m.phase_minutes==1);
    uint64_t deadline=m.deadline_ms;
    m.screen_on=false;tap(&m,V_SETTINGS,1010);
    assert(m.timer==V_TIMER_PAUSED && m.remaining_ms==deadline-1060 && m.screen_on);
    uint64_t remaining=m.remaining_ms;m.focus_minutes=17;
    m.screen_on=false;tap(&m,V_SETTINGS,2010);
    assert(m.timer==V_TIMER_RUNNING && m.deadline_ms==2060+remaining && m.phase_minutes==1);
    deadline=m.deadline_ms;m.screen_on=false;
    hold(&m,V_SETTINGS,3010);assert(m.page==V_FUNCTIONS && m.deadline_ms==deadline && m.timer==V_TIMER_RUNNING);
    key(&m,V_SETTINGS,V_CLICK,3800);assert(m.page==V_FUNCTIONS); /* A late click cannot enter settings. */
    hold(&m,V_SETTINGS,4000);assert(m.page==V_HOME);
}
static void handoff_and_chime(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.focus_minutes=1;m.break_minutes=2;m.screen_seconds=3;
    hold(&m,V_DOWN,0);tap(&m,V_SETTINGS,800);uint64_t done=m.deadline_ms;
    assert(vibe_model_tick(&m,done)==V_ALARM && m.timer==V_TIMER_FINISHED && m.timer_break && m.completed==1);
    assert(vibe_model_completion_visible(&m,done+9999));vibe_model_tick(&m,done+3000);assert(m.screen_on);
    assert(!vibe_model_completion_visible(&m,done+10000));vibe_model_tick(&m,done+10000);assert(!m.screen_on);
    assert(vibe_model_tick(&m,done+600000)==0 && m.timer==V_TIMER_FINISHED && m.completed==1);
    assert(vibe_model_remaining(&m,done+600000)==120000);
    tap(&m,V_SETTINGS,done+600001);assert(m.timer==V_TIMER_RUNNING && m.timer_break && m.phase_minutes==2);
    uint64_t break_end=m.deadline_ms;m.break_minutes=8;
    assert(m.phase_minutes==2 && m.deadline_ms==break_end);
    m.recording=true;assert(vibe_model_tick(&m,break_end)==0 && m.alarm_pending && !m.timer_break);
    key(&m,V_UP,V_RELEASE,break_end+1);assert(vibe_model_tick(&m,break_end+1)==V_ALARM);
    assert(vibe_model_tick(&m,break_end+2)==0 && m.completed==1);
    hold(&m,V_SETTINGS,break_end+3);assert(m.page==V_FUNCTIONS && m.timer==V_TIMER_FINISHED);
    m.page=V_HOME;tap(&m,V_SETTINGS,break_end+1000);assert(m.timer==V_TIMER_RUNNING && !m.timer_break);
    /* A pending chime never competes with provisioning. */
    m.deadline_ms=break_end+2000;m.page=V_PROVISION;
    assert(vibe_model_tick(&m,break_end+2000)==0 && m.alarm_pending);
    m.page=V_HOME;assert(vibe_model_tick(&m,break_end+2001)==V_ALARM);
}
static void end_confirmation(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.voice_ready=true;m.enter_ready=true;
    hold(&m,V_DOWN,0);
    tap(&m,V_SETTINGS,3000);uint64_t deadline=m.deadline_ms;
    hold(&m,V_DOWN,3100);assert(m.page==V_POMODORO && m.timer==V_TIMER_RUNNING);
    assert(key(&m,V_DOWN,V_CLICK,3900)==0 && m.page==V_POMODORO); /* Entry release cannot exit. */
    assert(tap(&m,V_SETTINGS,3910)==0 && m.page==V_POMODORO && m.timer==V_TIMER_RUNNING);
    assert(tap(&m,V_UP,4000)==0 && m.page==V_HOME && m.deadline_ms==deadline && m.timer==V_TIMER_RUNNING);
    tap(&m,V_SETTINGS,4100);assert(m.timer==V_TIMER_PAUSED);uint64_t left=m.remaining_ms;
    hold(&m,V_DOWN,4200);tap(&m,V_SETTINGS,5100);
    assert(m.page==V_POMODORO && m.timer==V_TIMER_PAUSED && m.remaining_ms==left);
    assert(tap(&m,V_UP,5200)==0 && m.page==V_HOME && m.timer==V_TIMER_PAUSED);
    hold(&m,V_DOWN,5300);
    assert(tap(&m,V_DOWN,6200)==0 && m.page==V_HOME && m.timer==V_TIMER_READY);
    assert(!m.deadline_ms && !m.timer_break && !m.alarm_pending && !m.completion_until_ms);
    tap(&m,V_SETTINGS,6300);assert(m.timer==V_TIMER_READY);
    /* Waiting for the next phase exits directly and retains completed rounds. */
    hold(&m,V_DOWN,8300);tap(&m,V_SETTINGS,9200);
    uint64_t done=m.deadline_ms;assert(vibe_model_tick(&m,done)==V_ALARM);
    hold(&m,V_DOWN,done+1);
    assert(m.page==V_HOME && m.timer==V_TIMER_READY && !m.timer_break && m.completed==1);
    assert(!m.completion_until_ms && !m.alarm_pending);
    /* A defensive idle confirmation can only return Home. */
    m.page=V_POMODORO;assert(tap(&m,V_UP,done+2100)==0 && m.page==V_HOME && m.timer==V_TIMER_READY);
}
static void app_toggle(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.enter_ready=true;m.voice_ready=true;m.screen_on=false;
    key(&m,V_DOWN,V_PRESS,1);key(&m,V_DOWN,V_LONG,701);
    assert(m.timer==V_TIMER_ARMED && m.page==V_HOME && m.screen_on);
    key(&m,V_DOWN,V_LONG,1401);assert(m.timer==V_TIMER_ARMED); /* Held key cannot undo entry. */
    key(&m,V_DOWN,V_RELEASE,1500);assert(key(&m,V_DOWN,V_CLICK,1500)==0);
    assert(key(&m,V_DOWN,V_LONG,1550)==0 && m.timer==V_TIMER_ARMED);
    m.screen_on=false;hold(&m,V_DOWN,1600);
    assert(m.timer==V_TIMER_READY && m.page==V_HOME && m.screen_on && !m.deadline_ms);
    assert(key(&m,V_DOWN,V_LONG,2400)==0 && m.timer==V_TIMER_READY); /* No immediate re-entry. */
    assert(key(&m,V_DOWN,V_CLICK,2450)==0); /* Exit hold cannot send Enter. */
    assert(tap(&m,V_DOWN,2500)==V_ENTER);
    hold(&m,V_DOWN,2600);assert(m.timer==V_TIMER_ARMED);
    assert(key(&m,V_UP,V_PRESS,3400)==V_START_AUDIO);
    hold(&m,V_DOWN,3500);assert(m.timer==V_TIMER_ARMED && m.recording);
    assert(key(&m,V_UP,V_RELEASE,4300)==V_STOP_AUDIO);
    hold(&m,V_DOWN,4400);assert(m.timer==V_TIMER_READY);
    /* Both waiting phases, including a sleeping screen, leave in one hold. */
    for (int rest=0;rest<2;rest++) {
        m.timer=V_TIMER_FINISHED;m.timer_break=rest;m.completed=3;m.screen_on=false;
        m.phase_minutes=25;m.completion_until_ms=90000;m.alarm_pending=true;
        hold(&m,V_DOWN,5400+rest*1000);
        assert(m.timer==V_TIMER_READY && m.page==V_HOME && m.completed==3 && !m.timer_break);
        assert(!m.phase_minutes && !m.completion_until_ms && !m.alarm_pending);
    }
}
static void settings_bounds(void) {
    vibe_model_t m;vibe_model_init(&m,0);hold(&m,V_SETTINGS,0);
    tap(&m,V_SETTINGS,1100);assert(m.page==V_SETTINGS_PAGE);
    m.selected=2;m.editing=true;m.screen_seconds=3;
    assert(tap(&m,V_DOWN,1200)==V_PREFS_CHANGED && m.screen_seconds==3);
    m.screen_seconds=60;assert(tap(&m,V_UP,1300)==V_PREFS_CHANGED && m.screen_seconds==60);
    hold(&m,V_SETTINGS,1400);assert(!m.editing && m.page==V_SETTINGS_PAGE);
    m.selected=V_SETTING_UNPAIR;
    tap(&m,V_DOWN,2200);assert(m.selected==V_SETTING_UNPAIR); /* Last item is unpair. */
    hold(&m,V_SETTINGS,2300);assert(m.page==V_FUNCTIONS);

}
static unsigned enter_radio(vibe_model_t *m,uint64_t t) {
    m->wifi_available=true;
    hold(m,V_SETTINGS,t);tap(m,V_DOWN,t+800);return tap(m,V_SETTINGS,t+900);
}
static void radio_home(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.radio_count=3;m.voice_ready=m.enter_ready=true;
    assert(enter_radio(&m,0)==V_RADIO_CHANGED && m.radio_active && m.radio_requested && m.page==V_HOME);
    unsigned weather=m.weather_page;vibe_model_tick(&m,9000);assert(m.weather_page==weather);
    for (unsigned i=1;i<=3;i++) assert(tap(&m,V_SETTINGS,10000+i*100)==V_RADIO_STATION && m.radio_index==i%3);
    m.screen_on=false;assert(tap(&m,V_SETTINGS,11000)==V_RADIO_STATION && m.screen_on);
    assert(key(&m,V_UP,V_PRESS,12000)==V_START_AUDIO && m.radio_requested);
    assert(tap(&m,V_SETTINGS,12100)==0 && m.radio_index==1);
    assert(key(&m,V_UP,V_RELEASE,13000)==V_STOP_AUDIO && m.radio_requested);
    assert(tap(&m,V_DOWN,13100)==V_ENTER);
    m.voice_ready=false;assert(key(&m,V_UP,V_PRESS,13200)==0 && m.radio_requested);
    hold(&m,V_SETTINGS,14000);assert(m.radio_requested && m.page==V_FUNCTIONS);
    tap(&m,V_DOWN,14900);tap(&m,V_DOWN,15000);assert(m.selected==1);
    assert(tap(&m,V_SETTINGS,15100)==0 && m.radio_requested && m.radio_active && m.page==V_HOME);
    m.voice_ready=true;key(&m,V_UP,V_PRESS,16000);key(&m,V_UP,V_RELEASE,16100);assert(m.radio_requested);
    assert(hold(&m,V_DOWN,17000)==0 && m.radio_active && m.page==V_RADIO_EXIT);
    assert(key(&m,V_DOWN,V_CLICK,17800)==0 && m.radio_active);
    assert(tap(&m,V_DOWN,17810)==V_RADIO_CHANGED && !m.radio_active && m.timer==V_TIMER_READY);
    hold(&m,V_DOWN,18000);tap(&m,V_SETTINGS,18800);uint64_t deadline=m.deadline_ms;
    assert(enter_radio(&m,19000)==0 && m.switch_radio && m.page==V_POMODORO && !m.radio_active);
    vibe_model_tick(&m,21000);tap(&m,V_UP,22000);
    assert(!m.switch_radio && !m.radio_active && m.timer==V_TIMER_RUNNING && m.deadline_ms==deadline);
    enter_radio(&m,23000);assert(tap(&m,V_DOWN,25000)==V_RADIO_CHANGED && m.radio_active && m.timer==V_TIMER_READY);
    hold(&m,V_DOWN,26000);tap(&m,V_DOWN,26800);m.radio_count=0;
    assert(enter_radio(&m,27000)==V_RADIO_CHANGED && m.radio_active && !m.radio_requested);
    assert(tap(&m,V_SETTINGS,29000)==0 && m.radio_index==0);
    vibe_model_t restored=m;restored.radio_count=3;
    assert(enter_radio(&restored,30000)==V_RADIO_CHANGED && restored.radio_requested && restored.page==V_HOME);
    hold(&m,V_DOWN,30000);tap(&m,V_DOWN,30800);hold(&m,V_DOWN,31000);assert(m.timer==V_TIMER_ARMED);
    assert(enter_radio(&m,32000)==V_RADIO_CHANGED && m.radio_active && m.timer==V_TIMER_READY);
}
static void radio_exit_confirmation(void) {
    for (unsigned sleeping=0;sleeping<2;sleeping++) for (unsigned requested=0;requested<2;requested++) {
        vibe_model_t m;vibe_model_init(&m,0);m.voice_ready=m.enter_ready=true;
        m.radio_active=true;m.radio_requested=requested;m.radio_count=requested?3:0;
        m.radio_index=requested?2:0;m.weather_page=2;m.screen_on=!sleeping;
        unsigned station=m.radio_index;
        assert(hold(&m,V_DOWN,100)==0 && m.page==V_RADIO_EXIT && m.screen_on);
        assert(m.radio_active && m.radio_requested==requested && m.radio_index==station && m.weather_page==(sleeping?0U:2U));
        assert(key(&m,V_DOWN,V_LONG,900)==0 && m.page==V_RADIO_EXIT);
        assert(key(&m,V_DOWN,V_CLICK,950)==0 && m.page==V_RADIO_EXIT); /* Entry release cannot confirm. */
        assert(tap(&m,V_SETTINGS,1000)==0 && m.page==V_RADIO_EXIT && m.radio_index==station);
        assert(hold(&m,V_SETTINGS,1100)==0 && m.page==V_RADIO_EXIT);
        assert(tap(&m,V_UP,2000)==0 && m.page==V_HOME); /* Cancel never starts voice. */
        assert(m.radio_active && m.radio_requested==requested && m.radio_index==station && !m.recording);
        assert(tap(&m,V_DOWN,2100)==V_ENTER); /* Home mappings resume immediately. */
        assert(hold(&m,V_DOWN,2200)==0 && m.page==V_RADIO_EXIT);
        assert(key(&m,V_DOWN,V_CLICK,3000)==0 && m.page==V_RADIO_EXIT);
        assert(hold(&m,V_DOWN,3100)==0 && m.page==V_RADIO_EXIT); /* Confirmation requires a short press. */
        assert(key(&m,V_DOWN,V_CLICK,3900)==0 && m.page==V_RADIO_EXIT);
        assert(tap(&m,V_DOWN,4000)==V_RADIO_CHANGED && m.page==V_HOME);
        assert(!m.radio_active && !m.radio_requested && m.radio_index==station && m.weather_page==0);
        assert(m.timer==V_TIMER_READY && !m.recording);
        assert(key(&m,V_DOWN,V_RELEASE,4100)==0);
    }
}
static void theme_controls(void) {
    vibe_model_t m;vibe_model_init(&m,0);
    assert(m.theme_mode==V_THEME_SCHEDULED);
    hold(&m,V_DOWN,0);tap(&m,V_SETTINGS,800);uint64_t deadline=m.deadline_ms;
    hold(&m,V_SETTINGS,1000);tap(&m,V_SETTINGS,1800);assert(m.page==V_SETTINGS_PAGE);
    for (unsigned i=0;i<V_SETTING_THEME;i++) tap(&m,V_DOWN,1900+i*100);
    assert(m.selected==V_SETTING_THEME && !m.editing && m.theme_mode==V_THEME_SCHEDULED);
    assert(tap(&m,V_SETTINGS,2300)==0 && m.editing);
    const vibe_theme_mode_t next[]={V_THEME_DARK,V_THEME_LIGHT,V_THEME_SCHEDULED};
    for (int i=0;i<3;i++) {
        assert(tap(&m,V_DOWN,2400+i*100)==V_PREFS_CHANGED && m.theme_mode==next[i]);
        assert(m.timer==V_TIMER_RUNNING && m.deadline_ms==deadline);
    }
    assert(tap(&m,V_UP,2800)==V_PREFS_CHANGED && m.theme_mode==V_THEME_LIGHT);
    assert(tap(&m,V_UP,2900)==V_PREFS_CHANGED && m.theme_mode==V_THEME_DARK);
    assert(tap(&m,V_UP,3000)==V_PREFS_CHANGED && m.theme_mode==V_THEME_SCHEDULED);
    assert(m.brightness==50 && m.volume==80 && m.screen_seconds==15);
    assert(tap(&m,V_SETTINGS,3100)==0 && !m.editing);
    for (unsigned i=V_SETTING_THEME;i<V_SETTING_WIFI;i++) tap(&m,V_DOWN,3200+i*10);
    assert(m.selected==V_SETTING_WIFI);
    assert(tap(&m,V_SETTINGS,3300)==V_START_PROVISION);
    tap(&m,V_DOWN,3400);assert(m.selected==V_SETTING_UNPAIR);
    assert(tap(&m,V_SETTINGS,3500)==0 && m.reset_confirm);
    assert(tap(&m,V_SETTINGS,3600)==V_UNPAIR && !m.reset_confirm);
    hold(&m,V_SETTINGS,3700);assert(m.page==V_FUNCTIONS && m.timer==V_TIMER_RUNNING);
}
static void exit_preserves_time(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.focus_minutes=1;
    hold(&m,V_DOWN,0);tap(&m,V_SETTINGS,800);uint64_t deadline=m.deadline_ms;
    hold(&m,V_DOWN,1000);
    assert(m.page==V_POMODORO && m.timer==V_TIMER_RUNNING);
    vibe_model_tick(&m,5000);
    assert(vibe_model_remaining(&m,5000)==deadline-5000 && m.deadline_ms==deadline);
    tap(&m,V_UP,6000);
    assert(m.page==V_HOME && m.timer==V_TIMER_RUNNING && m.deadline_ms==deadline);
    tap(&m,V_SETTINGS,7000);uint64_t left=m.remaining_ms;
    hold(&m,V_DOWN,8000);vibe_model_tick(&m,11000);tap(&m,V_UP,12000);
    assert(m.page==V_HOME && m.timer==V_TIMER_PAUSED && m.remaining_ms==left);
    tap(&m,V_SETTINGS,13000);deadline=m.deadline_ms;
    hold(&m,V_DOWN,14000);
    assert(vibe_model_tick(&m,deadline)==V_ALARM && m.page==V_POMODORO);
    tap(&m,V_UP,deadline+1000);
    assert(m.page==V_HOME && m.timer==V_TIMER_FINISHED && m.timer_break && m.completed==1);
}
static void weather_navigation(void) {
    vibe_model_t m;vibe_model_init(&m,0);m.voice_ready=m.enter_ready=true;
    vibe_model_tick(&m,7999);assert(m.weather_page==0);
    vibe_model_tick(&m,8000);assert(m.weather_page==0 && m.last_activity_ms==0);
    tap(&m,V_SETTINGS,9000);assert(m.weather_page==1);
    m.screen_seconds=60;vibe_model_tick(&m,30000);assert(m.weather_page==1);
    tap(&m,V_SETTINGS,31000);tap(&m,V_SETTINGS,32000);assert(m.weather_page==3);
    tap(&m,V_SETTINGS,32500);assert(m.weather_page==0);
    hold(&m,V_SETTINGS,33000);assert(m.page==V_FUNCTIONS && m.weather_page==0);
    key(&m,V_SETTINGS,V_CLICK,33800);assert(m.page==V_FUNCTIONS); /* No release flip. */
    hold(&m,V_SETTINGS,34000);assert(m.page==V_HOME);
    m.screen_on=false;tap(&m,V_SETTINGS,35000);assert(m.weather_page==0);
    m.screen_seconds=15;assert(key(&m,V_UP,V_PRESS,40000)==V_START_AUDIO);
    vibe_model_tick(&m,70000);assert(m.weather_page==0 && m.screen_on);
    key(&m,V_UP,V_RELEASE,71000);vibe_model_tick(&m,78999);assert(m.weather_page==0);
    vibe_model_tick(&m,79000);assert(m.weather_page==0);
    m.screen_on=false;hold(&m,V_SETTINGS,80000);assert(m.page==V_FUNCTIONS);
    m.page=V_HOME;hold(&m,V_DOWN,82000);tap(&m,V_SETTINGS,83000);assert(m.timer==V_TIMER_RUNNING);
    unsigned page=m.weather_page;vibe_model_tick(&m,84000);assert(m.weather_page==page);
    tap(&m,V_SETTINGS,85000);assert(m.timer==V_TIMER_PAUSED && m.weather_page==page);
    vibe_model_tick(&m,8*3600000ULL);assert(m.weather_page==page && m.timer==V_TIMER_PAUSED);
}
int main(void) {
    vibe_model_t idle;vibe_model_init(&idle,0);
    assert(vibe_model_wait_ms(&idle,100)==200);
    idle.screen_on=false;assert(vibe_model_wait_ms(&idle,100)==30000);
    idle.timer=V_TIMER_RUNNING;idle.deadline_ms=65000;
    assert(vibe_model_wait_ms(&idle,100)==30000);
    assert(vibe_model_wait_ms(&idle,64000)==1000);
    assert(vibe_model_wait_ms(&idle,65000)==1);
    assert(vibe_model_tick(&idle,65000)&V_ALARM);
    assert(idle.screen_on && vibe_model_wait_ms(&idle,65000)==200);
    weather_navigation();voice_and_enter();prepare_timer();timer_controls();handoff_and_chime();end_confirmation();settings_bounds();radio_home();radio_exit_confirmation();exit_preserves_time();app_toggle();theme_controls();
    puts("Vibe voice/Enter, app enter/exit per hold, waiting direct exit, active confirmation and manual handoff: PASS");
}
