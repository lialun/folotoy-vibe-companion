/* Full confirmation surface drawn over the current page. Keeping a second set
 * of LVGL widgets exhausted the 24 KiB pool on the hourly-weather background. */
static struct { bool visible,replacing,drawn;char name[49],request[37];uint32_t serial; } connection_surface;
static atomic_uint connection_presented_frame;
uint32_t vibe_ui_connection_frame(const char *request) {
    /* Only the controller writes the cached request; the LVGL task publishes
     * the completed refresh atomically. Button callbacks only read the ID. */
    if (request && (!connection_surface.visible || strcmp(request,connection_surface.request))) return 0;
    return atomic_load_explicit(&connection_presented_frame,memory_order_acquire);
}
static void connection_frame_finished(lv_event_t *event) {
    (void)event;
    if (connection_surface.visible && connection_surface.drawn) {
        atomic_store_explicit(&connection_presented_frame,connection_surface.serial,memory_order_release);
        connection_surface.drawn=false;
    }
}
static const lv_area_t connection_area={0,HOME_TOP,239,319};
static void connection_popup_draw(lv_event_t *event) {
    if (!connection_surface.visible) return;
    connection_surface.drawn=true;
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);
    rect.bg_opa=LV_OPA_COVER;rect.bg_color=lv_color_hex(BG);lv_draw_rect(layer,&rect,&connection_area);
    lv_draw_label_dsc_t text;lv_draw_label_dsc_init(&text);
    text.font=&body;text.color=lv_color_hex(INK);text.letter_space=-1;text.text_static=1;
    text.text=connection_surface.replacing?"切换到这台电脑？":"连接这台电脑？";
    const lv_area_t title={12,32,209,65};lv_draw_label(layer,&text,&title);
    text.font=&lv_font_montserrat_20;text.color=lv_color_hex(ACCENT);text.letter_space=0;text.text=connection_surface.name;
    const lv_area_t name={12,84,209,223};lv_draw_label(layer,&text,&name);
    text.font=&small;text.color=lv_color_hex(MUTED);text.letter_space=-1;
    text.text=connection_surface.replacing?"确认后断开原电脑":"";
    const lv_area_t hint={12,236,209,269};lv_draw_label(layer,&text,&hint);
    const int key_y[]={30,135};const char *keys[]={"确\n认","取\n消"};
    for (unsigned i=0;i<2;i++) {
        text.text=keys[i];text.line_space=-4;text.color=lv_color_hex(i?MUTED:ACCENT);
        lv_area_t key={KEY_RAIL_X,key_y[i],236,key_y[i]+43};lv_draw_label(layer,&text,&key);
        rect.bg_color=text.color;
        lv_area_t mark={KEY_RAIL_X+7,key_y[i]-6,KEY_RAIL_X+9,key_y[i]-4};lv_draw_rect(layer,&rect,&mark);
    }
}
static void connection_popup_refresh(const vibe_status_t *s) {
    bool visible=s->connection_request.phase==V_CONNECT_PENDING;
    if (connection_surface.visible!=visible || (visible &&
        (connection_surface.replacing!=s->connection_request.replacing || strcmp(connection_surface.name,s->connection_request.name) || strcmp(connection_surface.request,s->connection_request.request)))) {
        atomic_store_explicit(&connection_presented_frame,0,memory_order_release);
        connection_surface.drawn=false;
        if (++connection_surface.serial==0) ++connection_surface.serial;
        connection_surface.visible=visible;connection_surface.replacing=s->connection_request.replacing;
        snprintf(connection_surface.name,sizeof(connection_surface.name),"%s",s->connection_request.name);
        snprintf(connection_surface.request,sizeof(connection_surface.request),"%s",s->connection_request.request);
        lv_obj_invalidate_area(screen,&connection_area);
    }
}
