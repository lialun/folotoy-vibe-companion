/* Radio uses the existing label budget and a clipped text band. One bounded
 * position animation, no raster canvas, transform layer or perpetual motion. */
#include <stdatomic.h>
static lv_obj_t *radio_band;
/* The LVGL completion callback writes active; the main task also checks it
 * before taking the LVGL lock while asleep. The flag itself must be atomic. */
typedef struct { int incoming_y,outgoing_y;unsigned index;atomic_bool active; } radio_motion_t;
static radio_motion_t radio_motion;
static void radio_slide(void *state,int32_t distance) {
    (void)state;
    lv_obj_set_y(module_text[1],radio_motion.incoming_y+56-distance);
    lv_obj_set_y(module_text[5],radio_motion.outgoing_y-distance);
}
static void radio_slide_done(lv_anim_t *animation) {
    (void)animation;radio_motion.active=false;
    lv_obj_set_y(module_text[1],radio_motion.incoming_y);show(module_text[5],false);put(module_text[5],"");
}
static void radio_slide_stop(void) {
    lv_anim_delete(&radio_motion,radio_slide);
    if (radio_motion.active) {
        radio_motion.active=false;
        lv_obj_set_y(module_text[1],radio_motion.incoming_y);show(module_text[5],false);put(module_text[5],"");
    }
}
static void radio_scale_draw(lv_event_t *event) {
    lv_area_t area;lv_obj_get_coords(lv_event_get_target_obj(event),&area);
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);line.width=1;line.color=lv_color_hex(EDGE);
    for (unsigned i=0;i<=24;i++) {
        int x=area.x1+16+(int)i*8;
        line.p1=(lv_point_precise_t){x,area.y1+137-HOME_TOP};
        line.p2=(lv_point_precise_t){x,area.y1+137-HOME_TOP-(i%4?5:11)};
        lv_draw_line(layer,&line);
    }
    line.color=lv_color_hex(graphic_color(AMBER));line.width=2;
    line.p1=(lv_point_precise_t){area.x1+112,area.y1+118-HOME_TOP};
    line.p2=(lv_point_precise_t){area.x1+112,area.y1+140-HOME_TOP};
    lv_draw_line(layer,&line);
}
static void radio_create(void) {
    radio_motion=(radio_motion_t){0};
    // Relative to the module root. The band clips both sliding text objects,
    // keeping animation clear of the tuning scale and all fixed controls.
    radio_band=box(module_root,12,151-HOME_TOP,198,52,BG,0);
    for (unsigned i=1;i<=5;i+=4) {
        lv_obj_set_parent(module_text[i],radio_band);
        lv_obj_set_style_text_align(module_text[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_font(module_text[i],&body,0);
        lv_obj_set_style_text_line_space(module_text[i],0,0);
    }
    lv_obj_set_style_text_align(module_text[0],LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_set_style_text_align(module_text[2],LV_TEXT_ALIGN_CENTER,0);
    lv_obj_add_event_cb(module_root,radio_scale_draw,LV_EVENT_DRAW_POST,NULL);
}
static int radio_name_y(lv_obj_t *object,const char *name) {
    lv_point_t size;lv_text_get_size(&size,name,&body,-1,0,198,LV_TEXT_FLAG_NONE);
    int height=size.y<48?size.y:48;
    if (lv_obj_get_height(object)!=height) lv_obj_set_height(object,height);
    return (52-height)/2;
}
static void radio_update(const vibe_model_t *m,const vibe_status_t *s) {
    char count[24];
    const char *name=m->radio_count?s->radio_name:"暂无电台";
    bool changed=strcmp(lv_label_get_text(module_text[1]),name)!=0;
    bool switching=changed && lv_label_get_text(module_text[1])[0] && m->radio_count>1 &&
        radio_motion.index!=m->radio_index && !m->recording;
    if (switching) {
        radio_slide_stop();
        put(module_text[5],lv_label_get_text(module_text[1]));
        radio_motion.outgoing_y=radio_name_y(module_text[5],lv_label_get_text(module_text[5]));
        lv_obj_set_pos(module_text[5],0,radio_motion.outgoing_y);lv_obj_set_width(module_text[5],198);
    } else if (m->recording || changed) radio_slide_stop();
    put(module_heading,"收音机");lv_obj_set_pos(module_heading,12,86-HOME_TOP);lv_obj_set_width(module_heading,100);
    snprintf(count,sizeof(count),"%02u / %02u",m->radio_count?m->radio_index+1:0,m->radio_count);
    home_line(0,139,89,71,&numbers16,MUTED,count);
    put(module_text[1],name);
    int y=radio_name_y(module_text[1],name);radio_motion.incoming_y=y;
    int live_y=radio_motion.active && !switching?lv_obj_get_y(module_text[1]):y;
    home_line(1,0,HOME_TOP+live_y,198,&body,INK,name);
    if (switching) {
        radio_motion.active=true;show(module_text[5],true);
        lv_anim_t animation;lv_anim_init(&animation);
        lv_anim_set_var(&animation,&radio_motion);lv_anim_set_values(&animation,0,56);
        lv_anim_set_duration(&animation,200);lv_anim_set_exec_cb(&animation,radio_slide);
        lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);lv_anim_set_completed_cb(&animation,radio_slide_done);
        if (!lv_anim_start(&animation)) radio_slide_done(NULL);
    }
    if (radio_motion.active) home_lines|=1U<<5;
    radio_motion.index=m->radio_index;
    const char *state=m->recording?"语音输入中":!m->radio_count?"在客户端添加电台":
        s->radio_state==V_RADIO_ERROR?(s->radio_error[0]?s->radio_error:"连接失败"):
        !s->wifi?"等待 Wi-Fi":!m->radio_requested?"已停止":
        (s->radio_state==V_RADIO_CONNECTING || s->radio_state==V_RADIO_STOPPED)?"正在连接":
        s->radio_state==V_RADIO_BUFFERING?"正在缓冲":"正在播放";
    home_line(2,12,216,198,&small,s->radio_state==V_RADIO_ERROR?FOCUS_COLOR:AMBER,state);
    if (m->radio_count>1) {
        home_line(3,12,250,198,&small,MUTED,"下一台");
        home_line(4,12,274,198,&small,INK,s->radio_description);
    }
    for (unsigned i=0;i<module_count;i++) show(module_text[i],(home_lines&(1U<<i))!=0);
}
