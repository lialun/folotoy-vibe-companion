#include "vibe.h"
#include "radio/catalog_service.h"
#include "bsp_display.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef VIBE_UI_PREVIEW
#include <assert.h>
#endif
#include "ui_wave.inc"
#include "ui_timer.inc"
#include "ui_exit.inc"
#include "ui_weather_icons.inc"

LV_FONT_DECLARE(buddy_font_16);
LV_FONT_DECLARE(vibe_font_20);
static lv_font_t body,small,numbers16,numbers20,numbers36;
#define HOME_TOP 24
#define KEY_RAIL_X 221
typedef struct { uint32_t bg,panel,edge,ink,muted,accent,amber,focus,rest,weather; } palette_t;
/* Warm mineral canvas. Neutral text provides hierarchy; saturated inks mark
 * actionable states without relying on subtle differences between dark hues. */
static const palette_t dark_palette={0x14232E,0x203746,0x405867,0xF0EBDD,0xA8BBC2,0x51DFC4,0xF1C76D,0xFFB47D,0x8CC9FA,0xF2CB73};
static const palette_t light_palette={0xC6C1AD,0xB4B79C,0x94A68A,0x1B2529,0x3F4B50,0x005749,0x654600,0x8C2C0C,0x004E96,0x654600};
static const palette_t *palette=&dark_palette;
#define BG palette->bg
#define PANEL palette->panel
#define EDGE palette->edge
#define INK palette->ink
#define MUTED palette->muted
#define ACCENT palette->accent
#define AMBER palette->amber
#define FOCUS_COLOR palette->focus
#define REST_COLOR palette->rest
#define WEATHER_COLOR palette->weather
/* Keep small text dark; existing vector marks carry clearer chroma on the LCD.
 * Same semantic hues, with no additional objects, images or draw surfaces. */
static uint32_t graphic_color(uint32_t ink) {
    if (palette!=&light_palette) return ink;
    if (ink==ACCENT) return 0x00765D;
    if (ink==REST_COLOR) return 0x0965AD;
    if (ink==FOCUS_COLOR) return 0xB54716;
    if (ink==AMBER || ink==WEATHER_COLOR) return 0x936100;
    return ink;
}
static lv_obj_t *screen,*clock_text,*wifi_bars[3],*battery;
static lv_obj_t *home,*voice_state,*voice_hint,*voice_value,*wave_image;
static lv_obj_t *module_root,*module_heading,*module_value,*module_text[27],*progress,*progress_fill;
static int module_kind=-1;
static lv_obj_t *rail_name[3],*rail_mark[3],*timer_mark,*timer_image,*setting_extra;
static lv_obj_t *page_keys[3],*page_marks[3],*page_back;
static lv_obj_t *page,*page_title,*page_subtitle,*rows[V_SETTING_COUNT],*row_text[V_SETTING_COUNT],*row_value[V_SETTING_COUNT],*page_value;
static vibe_page_t drawn_page=(vibe_page_t)-1;
static void radio_slide_stop(void);
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius) {
    lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0); lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE); return o;
}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    if (font==&body || font==&small) lv_obj_set_style_text_letter_space(o,-1,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT); lv_label_set_text(o,""); return o;
}
static void put(lv_obj_t *o,const char *s) { if (strcmp(lv_label_get_text(o),s)) lv_label_set_text(o,s); }
static void show(lv_obj_t *o,bool yes) {
    bool hidden=lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN);
    if (yes && hidden) lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);
    if (!yes && !hidden) lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
static void tint(lv_obj_t *o,uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o,0),lv_color_hex(color))) lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
}
static void text_color(lv_obj_t *o,uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o,0),lv_color_hex(color))) lv_obj_set_style_text_color(o,lv_color_hex(color),0);
}
static void row(unsigned i,const char *name,const char *value,bool selected,bool editing) {
    show(rows[i],true); tint(rows[i],selected?EDGE:PANEL);
    int width=value[0]?120:184;
    if (lv_obj_get_width(row_text[i])!=width) lv_obj_set_width(row_text[i],width);
    char adjusted[96];
    if (editing) { snprintf(adjusted,sizeof(adjusted),LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT,value);value=adjusted; }
    put(row_text[i],name); put(row_value[i],value);
    /* Light rows use neutral text even when selected: the darker selection
     * background would reduce the contrast of a coloured 16 px value. */
    text_color(row_text[i],selected && palette==&dark_palette?ACCENT:INK);
    text_color(row_value[i],editing?BG:palette==&light_palette?INK:ACCENT);tint(row_value[i],ACCENT);
    lv_opa_t opacity=editing?LV_OPA_COVER:LV_OPA_TRANSP;
    if (lv_obj_get_style_bg_opa(row_value[i],0)!=opacity) lv_obj_set_style_bg_opa(row_value[i],opacity,0);
    lv_text_align_t align=editing?LV_TEXT_ALIGN_CENTER:LV_TEXT_ALIGN_RIGHT;
    if (lv_obj_get_style_text_align(row_value[i],0)!=align) lv_obj_set_style_text_align(row_value[i],align,0);
    if (lv_obj_get_style_border_width(rows[i],0)!=(editing?1:0)) lv_obj_set_style_border_width(rows[i],editing?1:0,0);
    if (!lv_color_eq(lv_obj_get_style_border_color(rows[i],0),lv_color_hex(ACCENT))) lv_obj_set_style_border_color(rows[i],lv_color_hex(ACCENT),0);
}
static void create_home(void) {
    home=box(screen,0,HOME_TOP,240,320-HOME_TOP,BG,0);
    wave_image=lv_image_create(home); lv_image_set_src(wave_image,&voice_wave_image); lv_obj_set_pos(wave_image,12,16);
    lv_obj_set_style_image_recolor(wave_image,lv_color_hex(ACCENT),0); lv_obj_set_style_image_recolor_opa(wave_image,LV_OPA_COVER,0);
    voice_state=label(home,52,18,161,&body,INK);
    voice_value=label(home,154,23,59,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(voice_value,LV_TEXT_ALIGN_RIGHT,0);
    voice_hint=label(home,52,32,161,&small,MUTED);
    module_root=NULL;module_kind=-1;
    /* Keep the labels centred on the physical keys, not on thirds of the LCD. */
    const int y[]={6,111,248};const char *names[]={"语\n音","回\n车","专\n注"};
    for (int i=0;i<3;i++) {
        rail_name[i]=label(home,KEY_RAIL_X,y[i],16,&small,MUTED);put(rail_name[i],names[i]);
        lv_obj_set_style_text_line_space(rail_name[i],-4,0);
        rail_mark[i]=box(home,KEY_RAIL_X+(i?7:3),y[i]-6,i?3:10,i?3:2,MUTED,0);
    }
    timer_mark=box(home,KEY_RAIL_X+3,152,10,2,MUTED,0);
    timer_image=lv_image_create(home);lv_image_set_src(timer_image,&timer_symbol_image);
    lv_obj_set_pos(timer_image,KEY_RAIL_X,158);
    lv_obj_set_style_image_recolor(timer_image,lv_color_hex(MUTED),0);lv_obj_set_style_image_recolor_opa(timer_image,LV_OPA_COVER,0);
    setting_extra=box(home,KEY_RAIL_X,214,16,24,BG,0);
    box(setting_extra,3,0,10,2,MUTED,0);
    lv_obj_t *settings_icon=label(setting_extra,0,6,16,&lv_font_montserrat_14,MUTED);put(settings_icon,LV_SYMBOL_SETTINGS);
}
static void create_page(vibe_page_t kind) {
    page=box(screen,0,HOME_TOP,240,320-HOME_TOP,BG,0);
    page_title=label(page,12,8,198,&body,INK);
    page_subtitle=label(page,12,38,198,&small,MUTED);
    page_value=label(page,12,70,198,&lv_font_montserrat_28,ACCENT);
    if (kind==V_POMODORO || kind==V_RADIO_EXIT) {
        lv_obj_set_pos(page_value,12,94);
        lv_obj_set_style_text_font(page_value,kind==V_RADIO_EXIT?&body:&lv_font_montserrat_48,0);
        if (kind==V_RADIO_EXIT) lv_obj_set_height(page_value,48);
    } else for (int i=0;i<(kind==V_FUNCTIONS?2:V_SETTING_COUNT);i++) {
        int top=(kind==V_FUNCTIONS || kind==V_SETTINGS_PAGE)?44:62;
        bool functions=kind==V_FUNCTIONS;
        rows[i]=box(page,8,functions?66+i*66:top+i*34,204,functions?58:28,PANEL,functions?0:6);
        row_text[i]=label(rows[i],functions?45:8,functions?17:0,functions?128:120,&body,INK);
        row_value[i]=label(rows[i],functions?12:132,functions?18:4,functions?23:62,&small,ACCENT);
        lv_obj_set_style_radius(row_value[i],3,0);
        lv_obj_set_style_text_align(row_value[i],LV_TEXT_ALIGN_RIGHT,0);
        if (functions) {
            lv_obj_set_style_border_side(rows[i],LV_BORDER_SIDE_LEFT,0);
            lv_obj_set_style_border_color(rows[i],lv_color_hex(ACCENT),0);
        }
    }
    const int key_y[]={6,111,248};
    for (int i=0;i<3;i++) {
        page_keys[i]=label(page,KEY_RAIL_X,key_y[i],16,&small,MUTED);
        lv_obj_set_style_text_line_space(page_keys[i],-4,0);
        page_marks[i]=box(page,KEY_RAIL_X+7,key_y[i]-6,3,3,MUTED,0);
    }
    page_back=box(page,KEY_RAIL_X,214,16,24,BG,0);
    box(page_back,3,0,10,2,MUTED,0);
    lv_obj_t *back=label(page_back,0,6,16,&lv_font_montserrat_14,MUTED);put(back,LV_SYMBOL_LEFT);
}
static void create_status(void) {
    for (int i=0;i<3;i++) wifi_bars[i]=box(screen,18+i*5,23-(4+i*3),3,4+i*3,ACCENT,1);
    clock_text=label(screen,40,6,108,&lv_font_montserrat_16,INK);
    // Reuse the existing label: right alignment anchors the icon while the
    // percentage grows leftwards, including 100%, inside the rounded inset.
    battery=label(screen,151,6,69,&lv_font_montserrat_16,INK);
    lv_obj_set_style_text_align(battery,LV_TEXT_ALIGN_RIGHT,0);
}
#include "ui_agent.inc"
void vibe_ui_init(void) {
    vibe_status_t s;vibe_snapshot(NULL,NULL,&s);
    palette=vibe_appearance_light(&s.appearance,time(NULL),s.tz_minutes)?&light_palette:&dark_palette;
    if (!bsp_lvgl_lock(1000)) return;
    body=vibe_font_20; body.fallback=&lv_font_montserrat_20;
    small=buddy_font_16; small.fallback=&lv_font_montserrat_14;
    numbers16=lv_font_montserrat_16;numbers16.fallback=&small;
    numbers20=lv_font_montserrat_20;numbers20.fallback=&body;
    numbers36=lv_font_montserrat_36;numbers36.fallback=&small;
    screen=box(NULL,0,0,240,320,BG,0);create_status();
    lv_obj_add_event_cb(screen,agent_popup_draw,LV_EVENT_DRAW_POST,NULL);
    lv_screen_load(screen); bsp_lvgl_unlock(); vibe_ui_refresh();
}
static unsigned home_lines;
static void home_line(unsigned i,int x,int y,int width,const lv_font_t *font,uint32_t color,const char *text) {
    lv_obj_t *o=module_text[i];home_lines|=1U<<i;
    if (lv_obj_get_x(o)!=x || lv_obj_get_y(o)!=y-HOME_TOP) lv_obj_set_pos(o,x,y-HOME_TOP);
    if (lv_obj_get_width(o)!=width) lv_obj_set_width(o,width);
    if (lv_obj_get_style_text_font(o,0)!=font) lv_obj_set_style_text_font(o,font,0);
    int spacing=(font==&body || font==&small)?-1:0;
    if (lv_obj_get_style_text_letter_space(o,0)!=spacing) lv_obj_set_style_text_letter_space(o,spacing,0);
    text_color(o,color);put(o,text);
}
#include "ui_weather.inc"
static unsigned module_count;
#include "ui_radio.inc"
static void create_module(int kind) {
    radio_slide_stop();
    if (module_root) lv_obj_delete(module_root);
    module_root=box(home,0,0,216,320-HOME_TOP,BG,0);lv_obj_move_to_index(module_root,0);
    module_heading=label(module_root,8,60,204,&body,INK);
    module_value=label(module_root,8,88,204,&lv_font_montserrat_48,INK);
    module_count=kind==6?6:kind==0?9:kind==1?9:kind==2?3:kind==3?10:kind==4?12:1;
    for (unsigned i=0;i<module_count;i++) {
        module_text[i]=label(module_root,8,120,204,&small,INK);
        if (kind>0) lv_label_set_long_mode(module_text[i],LV_LABEL_LONG_WRAP);
        if (kind==6 && (i==1 || i==4 || i==5)) {
            lv_obj_set_height(module_text[i],i==4?small.line_height:48);lv_label_set_long_mode(module_text[i],LV_LABEL_LONG_DOT);
        }
        if ((kind==1 && (i==1 || i==7)) || (kind==3 && (i==3 || i==5 || i==7 || i==9)))
            lv_obj_set_style_text_align(module_text[i],LV_TEXT_ALIGN_RIGHT,0);
    }
    if (kind==0) { progress=box(module_root,8,162,204,3,EDGE,0);progress_fill=box(progress,0,0,1,3,ACCENT,0); }
    else if (kind<5) weather_create((unsigned)kind-1);
    else show(module_value,false);
    if (kind==6) radio_create();
    module_kind=kind;
}
static void home_module(const vibe_model_t *m,const vibe_prefs_t *p,const vibe_status_t *s,uint64_t now) {
    char text[120];bool timer=m->timer!=V_TIMER_READY;
    bool weather=!m->radio_active && !timer && p->weather_enabled && s->forecast.valid;
    unsigned wp=m->weather_page%4;
    int kind=m->radio_active?6:timer?0:weather?(int)wp+1:5;
    if (module_kind!=kind) create_module(kind);
    home_lines=0;
    if (m->radio_active) {
        radio_update(m,s);
        return;
    }
    if (weather) {
        weather_update(wp,p,s,now);
        for (unsigned i=0;i<module_count;i++) show(module_text[i],(home_lines & (1U<<i))!=0);
        return;
    }
    if (!timer) {
        put(module_heading,p->city);home_line(0,8,120,204,&small,MUTED,"正在更新天气");return;
    }
    uint32_t phase_color=m->timer_break?REST_COLOR:FOCUS_COLOR;
    text_color(module_heading,phase_color);text_color(module_value,phase_color);tint(progress_fill,graphic_color(phase_color));
    show(progress,m->timer==V_TIMER_RUNNING || m->timer==V_TIMER_PAUSED);
    int value_y=m->timer==V_TIMER_FINISHED?118:88;
    if (lv_obj_get_y(module_value)!=value_y) lv_obj_set_y(module_value,value_y);
    if (m->timer==V_TIMER_ARMED) {
        put(module_heading,"专注待开始");
        snprintf(text,sizeof(text),"%02u:00",m->focus_minutes);put(module_value,text);
        snprintf(text,sizeof(text),"专注 %u 分 / 休息 %u 分",m->focus_minutes,m->break_minutes);
        home_line(0,8,173,204,&small,MUTED,text);
    } else if (m->timer==V_TIMER_FINISHED) {
        bool done=vibe_model_completion_visible(m,now);
        put(module_heading,done?(m->timer_break?"专注已完成":"休息已完成"):(m->timer_break?"休息待开始":"专注待开始"));
        home_line(0,8,115,204,&small,MUTED,m->timer_break?"接下来：休息":"接下来：专注");
        unsigned sec=(unsigned)(vibe_model_remaining(m,now)/1000);
        snprintf(text,sizeof(text),"%02u:%02u",sec/60,sec%60);put(module_value,text);
    } else if (timer) {
        snprintf(text,sizeof(text),"%s · 第 %u 轮",m->timer==V_TIMER_PAUSED?"已暂停":m->timer_break?"正在休息":"正在专注",m->completed+(m->timer_break?0:1));put(module_heading,text);
        unsigned sec=(unsigned)((vibe_model_remaining(m,now)+999)/1000);
        snprintf(text,sizeof(text),"%02u:%02u",sec/60,sec%60);put(module_value,text);
        uint64_t total=(m->phase_minutes?m->phase_minutes:(m->timer_break?m->break_minutes:m->focus_minutes))*60000ULL;
        uint64_t left=vibe_model_remaining(m,now);if (left>total) total=left;
        int width=total?(int)((total-left)*204/total):204;if (width<1) width=1;
        if (lv_obj_get_width(progress_fill)!=width) lv_obj_set_width(progress_fill,width);
        snprintf(text,sizeof(text),"%s %u 分钟 → %s",m->timer_break?"休息":"专注",m->phase_minutes?m->phase_minutes:(m->timer_break?p->break_minutes:p->focus_minutes),m->timer_break?"专注":"休息");
        home_line(0,8,207,204,&small,MUTED,text);
    }
    if (timer) { snprintf(text,sizeof(text),"已完成 %u 轮",m->completed);home_line(7,8,274,204,&small,MUTED,text); }
    for (unsigned i=0;i<module_count;i++) show(module_text[i],(home_lines & (1U<<i))!=0);
}
static void subtitle(const vibe_model_t *m,const vibe_status_t *s,const char *value) {
    (void)m;(void)s;put(page_subtitle,value);text_color(page_subtitle,MUTED);
}
void vibe_ui_refresh(void) {
    if (!screen) return;
    vibe_model_t m; vibe_status_t s; vibe_prefs_t p; vibe_snapshot(&m,&p,&s);
    if (!m.screen_on) {
        if (radio_motion.active && bsp_lvgl_lock(100)) { radio_slide_stop();bsp_lvgl_unlock(); }
        return;
    }
    if (!bsp_lvgl_lock(100)) return;
    uint64_t now=vibe_now(); char text[256],clock[32]="--/-- --:--";
    time_t t=time(NULL)+s.tz_minutes*60;
    if (t>1700000000) { struct tm tm; gmtime_r(&t,&tm); strftime(clock,sizeof(clock),"%m/%d %H:%M",&tm); }
    const palette_t *next=vibe_appearance_light(&s.appearance,time(NULL),s.tz_minutes)?&light_palette:&dark_palette;
    if (next!=palette) {
        /* One screen at a time: the C3 has no PSRAM and a small LVGL pool. */
        radio_slide_stop();palette=next;lv_obj_clean(screen);tint(screen,BG);home=NULL;page=NULL;drawn_page=(vibe_page_t)-1;create_status();
    }
    put(clock_text,clock);
    int strength=s.wifi && s.signal.valid?(int)s.signal.bars:0;
    for (int i=0;i<3;i++) tint(wifi_bars[i],i<strength?graphic_color(ACCENT):EDGE);
    const char *battery_icon=s.battery<0?LV_SYMBOL_BATTERY_EMPTY:s.battery>80?LV_SYMBOL_BATTERY_FULL:s.battery>55?LV_SYMBOL_BATTERY_3:s.battery>30?LV_SYMBOL_BATTERY_2:s.battery>10?LV_SYMBOL_BATTERY_1:LV_SYMBOL_BATTERY_EMPTY;
    if (s.battery>=0 && s.battery<=100) snprintf(text,sizeof(text),"%d%% %s",s.battery,battery_icon);
    else snprintf(text,sizeof(text),"--%% %s",LV_SYMBOL_BATTERY_EMPTY);
    put(battery,text);
    if (drawn_page!=m.page) {
        radio_slide_stop();
        if (home) { lv_obj_delete(home); home=NULL; }
        if (page) { lv_obj_delete(page); page=NULL; }
        if (m.page==V_HOME) create_home(); else create_page(m.page);
        drawn_page=m.page;
    }
    if (m.page==V_HOME) {
        const char *state="语音输入就绪",*hint="",*value="";
        if (m.recording) { state="正在输入";hint="松开上键，完成输入";snprintf(text,sizeof(text),"%02u:%02u",(unsigned)((now-m.recording_since_ms)/60000),(unsigned)((now-m.recording_since_ms)/1000%60));value=text; }
        else if (!s.wifi) { state="等待网络";hint="Wi-Fi 正在重新连接"; }
        else if (!s.token[0]) { state="连接电脑";hint="在客户端输入配对码";value=s.pair_code; }
        else if (!s.peer) { state="等待电脑";hint="请打开伴侣客户端"; }
        else if (!s.audio_ready || !s.hotkey_ready) { state="语音未就绪";hint="在客户端完成语音设置"; }
        else if (s.audio_fault) { state="请再试一次";hint="按住上键重试音频"; }
        put(voice_value,value);put(voice_state,state);put(voice_hint,hint);
        uint32_t voice_color=m.voice_ready || m.recording?ACCENT:MUTED;
        text_color(voice_state,m.voice_ready || m.recording?ACCENT:INK);text_color(voice_value,ACCENT);
        uint32_t wave_color=graphic_color(voice_color);
        if (!lv_color_eq(lv_obj_get_style_image_recolor(wave_image,0),lv_color_hex(wave_color)))
            lv_obj_set_style_image_recolor(wave_image,lv_color_hex(wave_color),0);
        bool two_lines=hint[0]!=0;
        int state_y=two_lines?6:18,value_y=two_lines?11:23;
        if (lv_obj_get_y(voice_state)!=state_y) lv_obj_set_y(voice_state,state_y);
        if (lv_obj_get_y(voice_value)!=value_y) lv_obj_set_y(voice_value,value_y);
        show(voice_hint,two_lines);
        put(rail_name[0],m.recording?"松\n开":"语\n音");
        put(rail_name[2],m.radio_active?"换\n台":m.timer==V_TIMER_READY?"翻\n页":m.timer==V_TIMER_ARMED?"开\n始":m.timer==V_TIMER_RUNNING?"暂\n停":m.timer==V_TIMER_PAUSED?"继\n续":m.timer_break?"休\n息":"专\n注");
        bool weather_home=m.timer==V_TIMER_READY && !m.radio_active;
        const lv_image_dsc_t *down_action=weather_home?&timer_symbol_image:&exit_symbol_image;
        if (lv_image_get_src(timer_image)!=down_action) lv_image_set_src(timer_image,down_action);
        if (lv_obj_get_width(rail_mark[2])!=3) {
            lv_obj_set_width(rail_mark[2],3);lv_obj_set_x(rail_mark[2],KEY_RAIL_X+7);lv_obj_set_height(rail_mark[2],3);
        }
        show(setting_extra,true);
        uint32_t rail_colors[]={m.voice_ready || m.recording?ACCENT:EDGE,
            m.enter_ready && !m.recording?REST_COLOR:EDGE,
            m.recording?EDGE:m.radio_active?AMBER:weather_home?MUTED:m.timer_break?REST_COLOR:FOCUS_COLOR};
        for (int i=0;i<3;i++) { text_color(rail_name[i],rail_colors[i]);tint(rail_mark[i],graphic_color(rail_colors[i])); }
        tint(timer_mark,m.recording?EDGE:graphic_color(FOCUS_COLOR));if (lv_obj_get_style_opa(setting_extra,0)!=(m.recording?LV_OPA_30:LV_OPA_COVER)) lv_obj_set_style_opa(setting_extra,m.recording?LV_OPA_30:LV_OPA_COVER,0);
        lv_color_t timer_color=lv_color_hex(m.recording?EDGE:graphic_color(FOCUS_COLOR));
        if (!lv_color_eq(lv_obj_get_style_image_recolor(timer_image,0),timer_color)) lv_obj_set_style_image_recolor(timer_image,timer_color,0);
        home_module(&m,&p,&s,now);
    } else {
        bool editing=m.page==V_SETTINGS_PAGE && m.editing,setup=m.page==V_PROVISION,exiting=m.page==V_POMODORO || m.page==V_RADIO_EXIT;
        bool theme_editing=editing && m.selected==V_SETTING_THEME;
        put(page_keys[0],exiting?"取\n消":setup?"—":theme_editing?"上\n个":editing?"增\n加":"上\n项");
        put(page_keys[1],exiting?(m.switch_radio?"切\n换":"退\n出"):setup?"—":theme_editing?"下\n个":editing?"减\n少":"下\n项");
        put(page_keys[2],exiting?"—":setup?"返\n回":editing?"完\n成":"确\n认");
        show(page_back,!setup && !exiting);
        for (int i=0;i<3;i++) {
            bool available=exiting?i<2:!setup || i==2;
            uint32_t color=exiting?(i==0?ACCENT:i==1?FOCUS_COLOR:EDGE):available?MUTED:EDGE;
            text_color(page_keys[i],color);tint(page_marks[i],graphic_color(color));show(page_marks[i],available);
        }
        if (lv_obj_get_width(page_marks[2])!=(setup?10:3)) {
            lv_obj_set_width(page_marks[2],setup?10:3);lv_obj_set_x(page_marks[2],KEY_RAIL_X+(setup?3:7));
        }
        if (!exiting) for (int i=0;i<(m.page==V_FUNCTIONS?2:V_SETTING_COUNT);i++) {
            bool visible=m.page==V_SETTINGS_PAGE || m.page==V_FUNCTIONS ||
                (m.page==V_PROVISION && (i==0 || i==1 || i==3 || i==4));
            show(rows[i],visible);
        }
        if (m.page!=V_POMODORO && m.page!=V_RADIO_EXIT) put(page_value,"");
        if (m.page==V_FUNCTIONS) {
            put(page_title,"更多功能"); subtitle(&m,&s,"");
            static const char *names[]={"设备设置","收音机"},*symbols[]={LV_SYMBOL_SETTINGS,LV_SYMBOL_AUDIO};
            for (unsigned i=0;i<2;i++) {
                bool selected=m.selected==i;
                put(row_text[i],names[i]);put(row_value[i],symbols[i]);
                tint(rows[i],selected?PANEL:BG);text_color(row_text[i],selected?INK:MUTED);text_color(row_value[i],selected?ACCENT:MUTED);
                if (lv_obj_get_style_border_width(rows[i],0)!=(selected?3:0)) lv_obj_set_style_border_width(rows[i],selected?3:0,0);
            }
        } else if (m.page==V_SETTINGS_PAGE) {
            put(page_title,"设备设置"); subtitle(&m,&s,"");
            const char *names[]={"屏幕亮度","设备音量","自动息屏","屏幕主题","重新配网","解除电脑配对"};
            const char *themes[]={"暗色","亮色","自动"};
            for (int i=0;i<V_SETTING_COUNT;i++) {
                text[0]=0;
                if (i<3) snprintf(text,sizeof(text),"%u%s",i==0?m.brightness:i==1?m.volume:m.screen_seconds,i==2?" 秒":"%");
                if (i==V_SETTING_THEME) snprintf(text,sizeof(text),"%s",themes[m.theme_mode<=V_THEME_SCHEDULED?m.theme_mode:V_THEME_SCHEDULED]);
                row(i,i==V_SETTING_UNPAIR&&m.reset_confirm?"确认解除配对？":names[i],text,m.selected==(unsigned)i,m.editing && m.selected==(unsigned)i);
            }
        } else if (m.page==V_POMODORO) {
            bool armed=m.timer==V_TIMER_ARMED;
            put(page_title,m.switch_radio?"结束番茄并收听？":"退出番茄？");
            subtitle(&m,&s,armed?"专注待开始":m.timer==V_TIMER_RUNNING?(m.timer_break?"正在休息":"正在专注"):m.timer==V_TIMER_PAUSED?(m.timer_break?"休息 · 已暂停":"专注 · 已暂停"):m.timer_break?"休息待开始":"专注待开始");
            unsigned sec=(unsigned)((vibe_model_remaining(&m,now)+999)/1000);
            snprintf(text,sizeof(text),"%02u:%02u",sec/60,sec%60);put(page_value,text);
            text_color(page_value,m.timer_break?REST_COLOR:FOCUS_COLOR);
        } else if (m.page==V_RADIO_EXIT) {
            put(page_title,"退出收音机？");
            const char *state=!m.radio_count?"暂无电台":!s.wifi?"等待 Wi-Fi":!m.radio_requested?"已停止":
                s.radio_state==V_RADIO_ERROR?"连接失败":s.radio_state==V_RADIO_PLAYING?"正在播放":
                s.radio_state==V_RADIO_BUFFERING?"正在缓冲":"正在连接";
            subtitle(&m,&s,state);put(page_value,m.radio_count?s.radio_name:"");text_color(page_value,INK);
        } else {
            put(page_title,"连接 Wi-Fi"); subtitle(&m,&s,"用手机完成，之后自动连接");
            row(0,"1  连接无密码热点","",true,false);
            row(1,s.ap_name,"",false,false);
            row(3,"2  选择 Wi-Fi","",false,false);
            row(4,"没弹出？192.168.4.1","",false,false);
        }
    }
    agent_popup_refresh(&m,&s);
    bsp_lvgl_unlock();
}
