/* Render the real firmware UI and fonts using the pinned LVGL software renderer.
 * Fixtures contain no device credentials and do not contact a device. */
#include "vibe.h"
#include "radio/catalog_service.h"
#include "lvgl.h"
#include "src/core/lv_global.h" /* Host-only, pinned TLSF peak instrumentation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static vibe_model_t model;
static vibe_prefs_t prefs;
static vibe_status_t status;
static uint64_t now=100000;
static time_t epoch=1794125280;
static void log_line(lv_log_level_t level,const char *text) { (void)level; fputs(text,stderr); }
static unsigned flushes;
static uint16_t pixels[240*320],stripe[240*16];
uint64_t vibe_now(void) { return now; }
time_t time(time_t *out) { time_t value=epoch; if (out) *out=value; return value; }
bool bsp_lvgl_lock(uint32_t timeout) { (void)timeout; return true; }
void bsp_lvgl_unlock(void) {}
void vibe_snapshot(vibe_model_t *m,vibe_prefs_t *p,vibe_status_t *s) {
    if (m) *m=model; if (p) *p=prefs; if (s) *s=status;
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    ++flushes;
    for (int y=area->y1;y<=area->y2;y++)
        memcpy(pixels+y*240+area->x1,data+(y-area->y1)*(area->x2-area->x1+1)*2,(area->x2-area->x1+1)*2);
    lv_display_flush_ready(display);
}
static void check_text_layout(lv_obj_t *object,const char *fixture) {
    if (lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(object,&lv_label_class)) {
        const char *text=lv_label_get_text(object);
        if (*text) {
            unsigned lines=1;
            for (const char *p=text;*p;p++) if (*p=='\n') ++lines;
            const lv_font_t *font=lv_obj_get_style_text_font(object,0);
            int expected=lines*font->line_height+(lines-1)*lv_obj_get_style_text_line_space(object,0);
            lv_area_t area;lv_obj_get_coords(object,&area);
            if (area.y1==6 && font==&lv_font_montserrat_16) {
                lv_point_t size;
                lv_text_get_size(&size,text,font,lv_obj_get_style_text_letter_space(object,0),
                    lv_obj_get_style_text_line_space(object,0),LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                assert(size.x<=lv_obj_get_width(object));
            }
            if (model.radio_active && lv_obj_get_height(object)==48) expected=font->line_height*2+lv_obj_get_style_text_line_space(object,0);
            if (lv_obj_get_height(object)>expected || area.x1<0 || area.x2>=240 || area.y1<0 || area.y2>=320) {
                fprintf(stderr,"%s: wrapped/clipped text '%s' at y=%d..%d height=%d expected=%d\n",
                    fixture,text,(int)area.y1,(int)area.y2,(int)lv_obj_get_height(object),expected);
                abort();
            }
        }
    }
    for (unsigned i=0;i<lv_obj_get_child_count(object);i++) check_text_layout(lv_obj_get_child(object,i),fixture);
}
static void capture(const char *directory,const char *name) {
    model.theme_mode=(vibe_theme_mode_t)status.appearance.mode;
    vibe_ui_refresh(); lv_refr_now(NULL);
    check_text_layout(lv_screen_active(),name);
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n240 320\n255\n");
    for (unsigned i=0;i<240*320;i++) {
        unsigned p=pixels[i]; uint8_t rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31}; fwrite(rgb,1,3,f);
    }
    fclose(f);
    unsigned previous=flushes; vibe_ui_refresh(); lv_refr_now(NULL);
    assert(flushes==previous); /* Unchanged UI must not redraw at the 200ms poll rate. */
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    printf("%s: LVGL used=%u/%u peak=%u bytes\n",name,(unsigned)(memory.total_size-memory.free_size),(unsigned)memory.total_size,(unsigned)memory.max_used);
}
static void down_hold(void) {
    vibe_model_key(&model,V_DOWN,V_PRESS,now);
    vibe_model_key(&model,V_DOWN,V_LONG,now+700);
    vibe_model_key(&model,V_DOWN,V_RELEASE,now+750);now+=750;
}
static void weather_fixture(void) {
    status.forecast=(vibe_weather_t){.valid=true,.is_day=true,.temperature=16,.feels=14,.humidity=62,.wind_valid=true,.gust_valid=true,.wind_ms=4,.gust_ms=8,.direction=315,.code=2,.sampled_at=epoch,.tz_seconds=28800,.hour_count=12};
    for (unsigned i=0;i<3;i++) status.forecast.days[i]=(vibe_forecast_t){.low=9-(int)i,.high=18-(int)i,.code=i==1?0:i==2?61:2,.rain=i==2?80:20,.wind_valid=true,.uv_valid=true,.uv_tenths=50,.wind_ms=6};
    for (unsigned i=0;i<12;i++) status.forecast.hours[i]=(vibe_hour_t){.at=epoch/3600*3600+i*3600,.temperature=16+(int)(i%3),.probability=(int)i*10,.rain_tenths=i%6?i%6:0,.fields=7};
    status.air=(vibe_air_t){.valid=true,.pm25_valid=true,.aqi_valid=true,.pm25_tenths=78,.aqi=42};status.air_at_ms=now;status.weather_at_ms=now;
    model.weather_page=0;
}
static unsigned object_count(lv_obj_t *object) {
    unsigned count=1;
    for (unsigned i=0;i<lv_obj_get_child_count(object);i++) count+=object_count(lv_obj_get_child(object,i));
    return count;
}
static void popup_checks(const char *directory) {
    static uint16_t original[240*320]; /* Host-only reference, never linked into firmware. */
    unsigned largest_draw_delta=0,largest_frame_peak=0;
    const char *contexts[]={"weather","hourly","air","forecast","timer","radio","settings","editing","exit","menu","radio-exit"};
    for (unsigned theme=0;theme<2;theme++) for (unsigned context=0;context<sizeof(contexts)/sizeof(contexts[0]);context++) {
        vibe_model_init(&model,now);model.voice_ready=true;model.enter_ready=true;
        status.peer=true;status.hotkey_ready=true;status.audio_fault=false;status.wifi=true;
        status.appearance.mode=theme?V_THEME_LIGHT:V_THEME_DARK;strcpy(prefs.city,"北京");weather_fixture();
        if (context<4) model.weather_page=context;
        if (context==4 || context==8) {
            model.timer=V_TIMER_RUNNING;model.phase_minutes=25;model.deadline_ms=now+24*60000+32000;
            if (context==8) model.page=V_POMODORO;
        }
        if (context==5 || context==10) {
            model.radio_active=true;model.radio_requested=true;model.radio_count=6;status.radio_state=V_RADIO_PLAYING;
            strcpy(status.radio_name,"怀旧音乐");strcpy(status.radio_description,"清晨音乐");
            if (context==10) model.page=V_RADIO_EXIT;
        }
        if (context==6 || context==7) { model.page=V_SETTINGS_PAGE;model.selected=V_SETTING_THEME;model.editing=context==7; }
        if (context==9) model.page=V_FUNCTIONS;
        model.theme_mode=(vibe_theme_mode_t)status.appearance.mode;
        memset(&status.agent_alert,0,sizeof(status.agent_alert));vibe_ui_refresh();lv_refr_now(NULL);
        /* Compare equally invalidated full frames on the same live page. Reset
         * only the host allocator's high-water statistic, never its allocation. */
        LV_GLOBAL_DEFAULT()->tlsf_state.max_used=0;
        lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
        lv_mem_monitor_t baseline_frame;lv_mem_monitor(&baseline_frame);
        memcpy(original,pixels,sizeof(original));
        lv_mem_monitor_t before,after;lv_mem_monitor(&before);
        unsigned objects=object_count(lv_screen_active());
        for (unsigned kind=0;kind<4;kind++) {
            char name[80];snprintf(name,sizeof(name),"%s-popup-%s-%u",theme?"light":"dark",contexts[context],kind);
            status.agent_alert.visible=true;status.agent_alert.active.agent=2;status.agent_alert.active.kind=kind;
            LV_GLOBAL_DEFAULT()->tlsf_state.max_used=0;
            lv_obj_invalidate(lv_screen_active());
            capture(directory,name);
            assert(object_count(lv_screen_active())==objects);
            lv_mem_monitor(&after);assert(after.total_size-after.free_size==before.total_size-before.free_size);
            unsigned draw_delta=after.max_used>baseline_frame.max_used?after.max_used-baseline_frame.max_used:0;
            if (draw_delta>largest_draw_delta) largest_draw_delta=draw_delta;
            if (after.max_used>largest_frame_peak) largest_frame_peak=after.max_used;
            unsigned changed=0;
            for (unsigned y=0;y<320;y++) for (unsigned x=0;x<240;x++) {
                bool inside=x>=8 && x<=211 && y>=118 && y<=197;
                if (!inside) assert(pixels[y*240+x]==original[y*240+x]);
                else if (pixels[y*240+x]!=original[y*240+x]) ++changed;
            }
            assert(changed>200); /* Ensure the notice really rendered, not just an unchanged background. */
            vibe_agent_dismiss(&status.agent_alert);vibe_ui_refresh();lv_refr_now(NULL);
            assert(!memcmp(original,pixels,sizeof(original)));
        }
        for (unsigned i=0;i<100;i++) {
            status.agent_alert.visible=true;status.agent_alert.active.agent=i%7;status.agent_alert.active.kind=i%4;
            vibe_ui_refresh();lv_refr_now(NULL);vibe_agent_dismiss(&status.agent_alert);vibe_ui_refresh();lv_refr_now(NULL);
        }
        lv_mem_monitor(&after);assert(after.total_size-after.free_size==before.total_size-before.free_size);
        assert(object_count(lv_screen_active())==objects && !memcmp(original,pixels,sizeof(original)));
    }
    printf("Popup matched full-frame draw: largest extra peak=%u bytes, largest frame peak=%u bytes\n",largest_draw_delta,largest_frame_peak);
    puts("Popup: all four events / both themes / eleven backgrounds, exact outside pixels, restoration and 2200 cycles: PASS");
}
static void change_radio(unsigned index) {
    static const char *names[]={"经济之声","文艺之声","怀旧音乐","清晨音乐"};
    model.radio_index=index%4;strcpy(status.radio_name,names[index%4]);
    vibe_ui_refresh();lv_refr_now(NULL);
}
static void radio_motion_checks(const char *directory) {
    vibe_model_init(&model,now);model.radio_active=model.radio_requested=true;model.radio_count=4;
    status.appearance.mode=V_THEME_DARK;status.wifi=status.peer=status.audio_ready=status.hotkey_ready=true;
    status.radio_state=V_RADIO_PLAYING;memset(&status.agent_alert,0,sizeof(status.agent_alert));
    strcpy(status.radio_description,"清晨音乐");strcpy(status.radio_name,"经济之声");
    capture(directory,"radio-motion-before");
    unsigned objects=object_count(lv_screen_active());
    lv_mem_monitor_t before,after;lv_mem_monitor(&before);
    uint16_t original[240*320];unsigned animation_delta=0;
    for (unsigned cycle=1;cycle<=100;cycle++) {
        change_radio(cycle);assert(lv_anim_count_running()==1);
        lv_mem_monitor(&after);unsigned used=after.total_size-after.free_size;
        unsigned base=before.total_size-before.free_size;
        if (used>base && used-base>animation_delta) animation_delta=used-base;
        memcpy(original,pixels,sizeof(original));
        for (unsigned frame=0;frame<15;frame++) {
            lv_tick_inc(20);lv_timer_handler();lv_refr_now(NULL);
            for (unsigned y=0;y<320;y++) for (unsigned x=0;x<240;x++)
                if (x<12 || x>209 || y<151 || y>202) assert(pixels[y*240+x]==original[y*240+x]);
        }
        assert(lv_anim_count_running()==0 && object_count(lv_screen_active())==objects);
        lv_mem_monitor(&after);
        // TLSF can retain one minimum-block remainder after equal-length
        // strings move. Bound that slack on every cycle, not just the last.
        assert(after.total_size-after.free_size<=before.total_size-before.free_size+32);
    }
    lv_mem_monitor(&after);
    printf("Radio steady pool before=%u after=%u, objects=%u/%u\n",(unsigned)(before.total_size-before.free_size),(unsigned)(after.total_size-after.free_size),objects,object_count(lv_screen_active()));
    assert(after.total_size-after.free_size<=before.total_size-before.free_size+32);
    capture(directory,"radio-motion-after");
    // Rapid switching has at most one live animation. Voice, standby, page
    // deletion and a live palette replacement must cancel it before teardown.
    change_radio(1);change_radio(2);assert(lv_anim_count_running()==1);
    model.recording=true;vibe_ui_refresh();assert(lv_anim_count_running()==0);model.recording=false;
    change_radio(3);model.screen_on=false;vibe_ui_refresh();assert(lv_anim_count_running()==0);
    model.screen_on=true;vibe_ui_refresh();change_radio(0);
    model.page=V_FUNCTIONS;vibe_ui_refresh();assert(lv_anim_count_running()==0);
    model.page=V_HOME;vibe_ui_refresh();change_radio(1);
    status.appearance.mode=V_THEME_LIGHT;vibe_ui_refresh();assert(lv_anim_count_running()==0);
    capture(directory,"radio-motion-light-restored");
    printf("Radio animation: temporary pool delta=%u bytes; 100 transitions, clipping, rapid input, voice/sleep/page/theme cancellation PASS\n",animation_delta);
}
static void radio_exit_checks(const char *directory) {
    for (unsigned theme=0;theme<2;theme++) {
        vibe_model_init(&model,now);model.voice_ready=model.enter_ready=true;
        model.radio_active=model.radio_requested=true;model.radio_count=4;model.radio_index=2;
        status.appearance.mode=theme?V_THEME_LIGHT:V_THEME_DARK;
        status.wifi=status.peer=status.audio_ready=status.hotkey_ready=true;status.radio_state=V_RADIO_PLAYING;
        memset(&status.agent_alert,0,sizeof(status.agent_alert));
        strcpy(status.radio_name,"文艺之声");strcpy(status.radio_description,"清晨音乐");
        capture(directory,theme?"light-radio-exit-before":"dark-radio-exit-before");
        down_hold();assert(model.page==V_RADIO_EXIT && model.radio_requested);
        capture(directory,theme?"light-radio-exit":"dark-radio-exit");
        strcpy(status.radio_name,"这是一个十六字的中文电台名称测试");
        capture(directory,theme?"light-radio-exit-long":"dark-radio-exit-long");strcpy(status.radio_name,"文艺之声");
        status.radio_state=V_RADIO_ERROR;capture(directory,theme?"light-radio-exit-error":"dark-radio-exit-error");status.radio_state=V_RADIO_PLAYING;
        model.radio_count=0;model.radio_requested=false;capture(directory,theme?"light-radio-exit-empty":"dark-radio-exit-empty");
        model.radio_count=4;model.radio_requested=true;
        unsigned baseline_used=0,baseline_objects=0;
        for (unsigned cycle=0;cycle<100;cycle++) {
            assert(vibe_model_key(&model,V_UP,V_PRESS,now)==0);
            assert(vibe_model_key(&model,V_UP,V_RELEASE,now+50)==0);
            assert(vibe_model_key(&model,V_UP,V_CLICK,now+50)==0);
            assert(model.page==V_HOME && model.radio_active && model.radio_requested && model.radio_index==2 && !model.recording);
            vibe_ui_refresh();lv_refr_now(NULL);
            lv_mem_monitor_t before,after;lv_mem_monitor(&before);unsigned objects=object_count(lv_screen_active());
            unsigned used=before.total_size-before.free_size;
            if (!cycle) { baseline_used=used;baseline_objects=objects; }
            assert(used<=baseline_used+32 && objects==baseline_objects);
            down_hold();assert(model.page==V_RADIO_EXIT && model.radio_requested);
            vibe_ui_refresh();lv_refr_now(NULL);lv_mem_monitor(&after);
            assert(after.total_size-after.free_size<=before.total_size-before.free_size);
            assert(object_count(lv_screen_active())<objects);
        }
        assert(vibe_model_key(&model,V_DOWN,V_CLICK,now)==0); /* The entry hold is still suppressed. */
        assert(vibe_model_key(&model,V_DOWN,V_PRESS,now+1)==0);
        assert(vibe_model_key(&model,V_DOWN,V_RELEASE,now+51)==0);
        assert(vibe_model_key(&model,V_DOWN,V_CLICK,now+51)==V_RADIO_CHANGED);
        assert(model.page==V_HOME && !model.radio_active && !model.radio_requested);
        capture(directory,theme?"light-radio-exit-confirmed":"dark-radio-exit-confirmed");
    }
    puts("Radio exit: both themes, long/empty/error names, 200 cancel cycles, smaller page pool and explicit stop: PASS");
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0); assert(argc==2 || argc==3); fputs("initializing LVGL\n",stderr); lv_init(); lv_log_register_print_cb(log_line); fputs("creating display\n",stderr); lv_display_t *d=lv_display_create(240,320); assert(d);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,stripe,NULL,sizeof(stripe),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(d,flush);
    vibe_model_init(&model,now); model.voice_ready=true; model.enter_ready=true;
    prefs=(vibe_prefs_t){.weather_enabled=true,.focus_minutes=25,.break_minutes=5,.volume=60}; strcpy(prefs.city,"北京");
    status=(vibe_status_t){.wifi=true,.peer=true,.audio_ready=true,.hotkey_ready=true,.battery=72,.rssi=-50,.tz_minutes=480,.weather_at_ms=now}; strcpy(status.token,"preview");
    status.appearance=vibe_appearance_default();status.appearance.mode=V_THEME_DARK;vibe_signal_update(&status.signal,status.rssi);
    weather_fixture();
    if (argc==3) {
        // Separate processes give actual zero-initialized UI globals, rather
        // than pretending an existing screen is a power cycle.
        bool day=!strcmp(argv[2],"--boot-day");
        bool unknown=!strcmp(argv[2],"--clock-after-boot");
        bool wake=!strcmp(argv[2],"--wake-across-boundary");
        assert(day || unknown || wake || !strcmp(argv[2],"--boot-night"));
        status.appearance.mode=V_THEME_SCHEDULED;status.tz_minutes=0;
        epoch=unknown?0:1700006400LL+(day?600:1320)*60;
        vibe_ui_init();lv_refr_now(NULL);
        if (unknown || wake) {
            assert(pixels[80*240+100]==lv_color_to_u16(lv_color_hex(0x14232E)));
            if (wake) { model.screen_on=false;epoch+=12*3600;vibe_ui_refresh(); }
            else epoch=1700006400LL+600*60;
            model.screen_on=true;day=true;
        }
        capture(argv[1],argv[2]+2);
        assert(pixels[80*240+100]==lv_color_to_u16(lv_color_hex(day?0xC6C1AD:0x14232E)));
        puts("Fresh boot / first valid clock / wake across missed schedule boundary: PASS");return 0;
    }

    fputs("creating UI\n",stderr); vibe_ui_init(); fputs("capturing UI\n",stderr); capture(argv[1],"home");
    const int battery_values[]={0,9,10,72,99,100,-1};
    for (unsigned theme=0;theme<2;theme++) {
        status.appearance.mode=theme?V_THEME_LIGHT:V_THEME_DARK;
        for (unsigned i=0;i<sizeof(battery_values)/sizeof(battery_values[0]);i++) {
            status.battery=battery_values[i];
            char name[48];snprintf(name,sizeof(name),"battery-%s-%d",theme?"light":"dark",status.battery);
            capture(argv[1],name);
        }
    }
    status.battery=72;status.appearance.mode=V_THEME_DARK;
    for (unsigned theme=0;theme<2;theme++) {
        status.appearance.mode=theme?V_THEME_LIGHT:V_THEME_DARK;
        vibe_agent_push(&status.agent_alert,theme+1,2,2,now,10000);
        vibe_agent_tick(&status.agent_alert,now,false);
        capture(argv[1],theme?"light-agent-alert":"dark-agent-alert");
        model.recording=true;capture(argv[1],theme?"light-agent-speaking":"dark-agent-speaking");model.recording=false;
        model.page=V_SETTINGS_PAGE;capture(argv[1],theme?"light-agent-settings":"dark-agent-settings");model.page=V_HOME;
        memset(&status.agent_alert,0,sizeof(status.agent_alert));
        capture(argv[1],theme?"light-agent-restored":"dark-agent-restored");
    }
    status.appearance.mode=V_THEME_DARK;
    const unsigned uv_values[]={23,50,70,90,110,500};
    for (unsigned theme=0;theme<2;theme++) {
        status.appearance.mode=theme?V_THEME_LIGHT:V_THEME_DARK;
        for (unsigned i=0;i<7;i++) {
            char name[48];status.forecast.days[0].uv_valid=i<6;
            if (i<6) status.forecast.days[0].uv_tenths=uv_values[i];
            snprintf(name,sizeof(name),"%s-uv-%u",theme?"light":"dark",i);capture(argv[1],name);
        }
    }
    status.appearance.mode=V_THEME_DARK;status.forecast.days[0].uv_valid=true;status.forecast.days[0].uv_tenths=50;
    model.weather_page=1;capture(argv[1],"weather-hourly");model.weather_page=2; capture(argv[1],"weather-details");status.air.valid=false;capture(argv[1],"weather-air-missing");status.air.valid=true;model.weather_page=3;capture(argv[1],"weather-forecast");model.weather_page=0;
    model.recording=true;model.recording_since_ms=now-2300;capture(argv[1],"speaking"); model.recording=false;capture(argv[1],"ready-again");
    model.timer=V_TIMER_ARMED;capture(argv[1],"prepared");
    model.recording=true;capture(argv[1],"prepared-speaking");model.recording=false;
    down_hold();assert(model.timer==V_TIMER_READY && model.page==V_HOME);capture(argv[1],"prepared-closed");
    down_hold();assert(model.timer==V_TIMER_ARMED && model.page==V_HOME);capture(argv[1],"prepared-reentered");
    model.phase_minutes=25;model.timer=V_TIMER_RUNNING;model.deadline_ms=now+24*60000+32000;capture(argv[1],"focus");
    model.page=V_POMODORO;capture(argv[1],"running-exit");model.page=V_HOME;
    model.recording=true;capture(argv[1],"focus-speaking");model.recording=false;
    model.timer=V_TIMER_PAUSED;model.remaining_ms=24*60000+32000;capture(argv[1],"paused");
    model.page=V_POMODORO;capture(argv[1],"paused-exit");model.page=V_HOME;
    model.timer=V_TIMER_FINISHED;model.timer_break=true;model.completed=2;model.completion_until_ms=now+10000;capture(argv[1],"complete");now+=10001;capture(argv[1],"rest-waiting");model.timer_break=false;model.completion_until_ms=now+10000;capture(argv[1],"focus-waiting");
    model.page=V_POMODORO;model.selected=1;model.reset_confirm=true;capture(argv[1],"timer-controls");model.reset_confirm=false;model.page=V_FUNCTIONS;capture(argv[1],"functions");model.page=V_SETTINGS_PAGE;capture(argv[1],"settings");
    model.page=V_HOME;model.timer=V_TIMER_READY;model.radio_active=true;model.radio_requested=true;model.radio_count=6;
    strcpy(status.radio_name,"怀旧音乐");strcpy(status.radio_description,"清晨音乐");
    const unsigned radio_states[]={V_RADIO_CONNECTING,V_RADIO_BUFFERING,V_RADIO_PLAYING,V_RADIO_ERROR};
    for (unsigned theme=0;theme<2;theme++) {
        status.appearance.mode=theme?V_THEME_LIGHT:V_THEME_DARK;
        for (unsigned i=0;i<4;i++) { char name[48];status.radio_state=radio_states[i];snprintf(name,sizeof(name),"%s-radio-%u",theme?"light":"dark",i);capture(argv[1],name); }
        model.radio_requested=false;capture(argv[1],theme?"light-radio-paused":"dark-radio-paused");model.radio_requested=true;
        model.recording=true;capture(argv[1],theme?"light-radio-speaking":"dark-radio-speaking");model.recording=false;
        model.radio_count=0;capture(argv[1],theme?"light-radio-empty":"dark-radio-empty");model.radio_count=6;
        strcpy(status.radio_name,"这是一个十六字的中文电台名称测试");capture(argv[1],theme?"light-radio-long-name":"dark-radio-long-name");strcpy(status.radio_name,"怀旧音乐");
        model.page=V_FUNCTIONS;model.selected=1;capture(argv[1],theme?"light-radio-menu":"dark-radio-menu");model.page=V_HOME;
    }
    model.radio_active=false;model.radio_requested=false;status.appearance.mode=V_THEME_DARK;
    model.page=V_POMODORO;model.switch_radio=true;model.timer=V_TIMER_RUNNING;model.deadline_ms=now+60000;
    capture(argv[1],"radio-switch-confirm");model.switch_radio=false;model.timer=V_TIMER_READY;
    model.page=V_PROVISION;strcpy(status.ap_name,"Vibe-Setup-A1B2C3");capture(argv[1],"wifi-setup");
    model.page=V_HOME;model.timer=V_TIMER_READY;model.completion_until_ms=0;status.peer=false;capture(argv[1],"disconnected");
    status.wifi=false;capture(argv[1],"network-reconnecting");status.wifi=true;
    status.token[0]=0;strcpy(status.pair_code,"123456");capture(argv[1],"pairing");
    status.peer=true;strcpy(status.token,"preview");strcpy(prefs.city,"呼和浩特");
    status.forecast.temperature=-40;status.forecast.feels=-45;status.forecast.code=99;
    status.forecast.days[0]=(vibe_forecast_t){.low=-40,.high=-30,.code=99,.rain=100,.wind_valid=true,.wind_ms=56.1f,.uv_valid=true,.uv_tenths=500};
    status.forecast.days[1]=status.forecast.days[2]=status.forecast.days[0];
    now=200000;status.weather_at_ms=now;capture(argv[1],"weather-extremes");
    model.weather_page=2;status.forecast.humidity=100;status.forecast.wind_ms=56.1f;status.air.aqi=1000;status.air.pm25_tenths=100000;status.air_at_ms=now;capture(argv[1],"air-extremes");
    model.weather_page=3;capture(argv[1],"forecast-extremes");model.weather_page=1;
    for (unsigned i=0;i<12;i++) { status.forecast.hours[i].temperature=-12+(int)(i%3);status.forecast.hours[i].probability=100;status.forecast.hours[i].rain_tenths=128-i*10; }
    capture(argv[1],"hourly-extremes");status.forecast.hours[2].fields=V_HOUR_PROB;capture(argv[1],"hourly-partial");
    epoch+=86400;capture(argv[1],"hourly-exhausted");epoch-=86400;model.weather_page=0;
    status.hotkey_ready=false;capture(argv[1],"voice-not-ready");
    status.hotkey_ready=true;status.audio_fault=true;capture(argv[1],"audio-retry");status.audio_fault=false;capture(argv[1],"ready-after-retry");
    model.page=V_SETTINGS_PAGE;model.selected=V_SETTING_UNPAIR;model.reset_confirm=true;capture(argv[1],"unpair-confirm");
    model.editing=true;capture(argv[1],"settings-editing");
    model.editing=false;model.selected=0;model.reset_confirm=false;capture(argv[1],"settings-browse");
    status.appearance.mode=V_THEME_LIGHT;capture(argv[1],"light-settings");
    model.selected=1;model.editing=true;capture(argv[1],"light-settings-editing");model.editing=false;
    model.selected=V_SETTING_THEME;model.editing=false;capture(argv[1],"light-theme-selected");
    model.editing=true;capture(argv[1],"light-theme-editing");
    status.appearance.mode=V_THEME_DARK;capture(argv[1],"dark-theme-editing");
    model.editing=false;capture(argv[1],"dark-theme-selected");model.editing=true;
    model.brightness=100;model.volume=100;model.screen_seconds=60;
    for (unsigned i=0;i<3;i++) {
        char name[48];model.selected=i;
        snprintf(name,sizeof(name),"dark-edit-max-%u",i);capture(argv[1],name);
        status.appearance.mode=V_THEME_LIGHT;snprintf(name,sizeof(name),"light-edit-max-%u",i);capture(argv[1],name);
        status.appearance.mode=V_THEME_DARK;
    }
    model.brightness=50;model.volume=80;model.screen_seconds=15;model.selected=V_SETTING_THEME;
    status.appearance.mode=V_THEME_SCHEDULED;capture(argv[1],"auto-theme-editing");
    status.appearance.mode=V_THEME_LIGHT;model.editing=false;
    model.page=V_HOME;model.timer=V_TIMER_READY;status.forecast.temperature=16;status.forecast.code=2;strcpy(prefs.city,"北京");
    weather_fixture();

    status.weather_at_ms=now;capture(argv[1],"light-home");
    model.weather_page=1;capture(argv[1],"light-weather-hourly");model.weather_page=2;capture(argv[1],"light-weather-details");model.weather_page=3;capture(argv[1],"light-weather-forecast");model.weather_page=0;
    model.recording=true;capture(argv[1],"light-speaking");model.recording=false;
    model.timer=V_TIMER_ARMED;capture(argv[1],"light-prepared");
    model.recording=true;capture(argv[1],"light-prepared-speaking");model.recording=false;
    down_hold();assert(model.timer==V_TIMER_READY && model.page==V_HOME);capture(argv[1],"light-prepared-closed");
    model.timer=V_TIMER_RUNNING;model.phase_minutes=25;model.deadline_ms=now+24*60000+32000;capture(argv[1],"light-focus");
    model.timer=V_TIMER_PAUSED;model.remaining_ms=24*60000+32000;capture(argv[1],"light-paused");
    model.timer=V_TIMER_FINISHED;model.timer_break=true;model.completion_until_ms=0;capture(argv[1],"light-rest-waiting");
    model.page=V_POMODORO;capture(argv[1],"light-waiting-exit");model.selected=1;capture(argv[1],"light-waiting-exit-no-selection");
    model.page=V_FUNCTIONS;capture(argv[1],"light-functions");model.page=V_PROVISION;capture(argv[1],"light-wifi-setup");
    model.page=V_HOME;model.timer=V_TIMER_READY;model.timer_break=false;status.tz_minutes=0;
    status.appearance.mode=V_THEME_SCHEDULED;epoch=1700006400+479*60;capture(argv[1],"auto-before-light");
    epoch+=60;capture(argv[1],"auto-light-start");epoch=1700006400+1260*60;capture(argv[1],"auto-dark-start");
    epoch=0;capture(argv[1],"auto-clock-unknown");
    epoch=1794125280;
    for (int i=0;i<100;i++) { status.appearance.mode=i%2?V_THEME_DARK:V_THEME_LIGHT;vibe_ui_refresh();lv_refr_now(NULL); }
    capture(argv[1],"after-100-theme-switches");
    for (int i=0;i<100;i++) { model.weather_page=i%4;vibe_ui_refresh();lv_refr_now(NULL); }
    capture(argv[1],"after-100-weather-switches");
    popup_checks(argv[1]);
    radio_motion_checks(argv[1]);
    radio_exit_checks(argv[1]);
    puts("Actual LVGL render fixtures, 100 live theme transitions and 100 weather transitions: PASS");
}
