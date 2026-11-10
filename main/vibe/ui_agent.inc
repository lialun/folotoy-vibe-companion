/* Compact, non-modal notice drawn after the existing page. No child objects,
 * image/snapshot buffers, independent timer or borrowed audio memory. */
static unsigned agent_popup_state;
static const lv_area_t agent_popup_area={8,118,211,197};
typedef struct {
    uint32_t panel,edge,ink,muted,tone[4],fill[4];
} agent_popup_palette_t;
static const agent_popup_palette_t agent_popup_dark={
    0x263E48,0x435B65,0xF0EBDD,0xB4C3C5,
    {0x71D5BB,0xEDC777,0x9BC6EB,0xF3AE91},
    {0x244F49,0x4D4937,0x304C61,0x53443F}
};
static const agent_popup_palette_t agent_popup_light={
    0xCCC6AF,0x8D947E,0x1B2529,0x3F4B50,
    {0x175447,0x71500C,0x254F70,0x873E29},
    {0xADBBA0,0xC8B889,0xB0BCC0,0xC9AE97}
};
static void agent_popup_draw(lv_event_t *event) {
    if (!agent_popup_state) return;
    unsigned agent=(agent_popup_state>>4)&7,kind=(agent_popup_state>>1)&3;
    const agent_popup_palette_t *colors=palette==&light_palette?&agent_popup_light:&agent_popup_dark;
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_draw_rect_dsc_t rect;lv_draw_rect_dsc_init(&rect);
    rect.bg_opa=LV_OPA_COVER;rect.bg_color=lv_color_hex(colors->panel);
    rect.border_width=1;rect.border_color=lv_color_hex(colors->edge);rect.radius=12;
    lv_draw_rect(layer,&rect,&agent_popup_area);
    const lv_area_t badge={21,142,52,173};
    rect.border_width=0;rect.radius=10;rect.bg_color=lv_color_hex(colors->fill[kind]);
    lv_draw_rect(layer,&rect,&badge);
    static const char *symbols[]={LV_SYMBOL_OK,LV_SYMBOL_WARNING,LV_SYMBOL_KEYBOARD,LV_SYMBOL_CLOSE};
    lv_draw_label_dsc_t text;lv_draw_label_dsc_init(&text);
    text.font=&lv_font_montserrat_20;text.text=symbols[kind];text.text_static=1;
    text.color=lv_color_hex(colors->tone[kind]);text.align=LV_TEXT_ALIGN_CENTER;
    const lv_area_t symbol={21,146,52,171};lv_draw_label(layer,&text,&symbol);
    text.font=&numbers16;text.text=vibe_agent_name(agent);text.color=lv_color_hex(colors->muted);
    text.align=LV_TEXT_ALIGN_LEFT;
    const lv_area_t source={64,129,197,150};lv_draw_label(layer,&text,&source);
    text.font=&body;text.text=vibe_agent_action(kind);text.color=lv_color_hex(colors->ink);text.letter_space=-1;
    const lv_area_t action={64,153,201,180};lv_draw_label(layer,&text,&action);
}
static void agent_popup_refresh(const vibe_model_t *m,const vibe_status_t *s) {
    unsigned next=s->agent_alert.visible && !m->recording && m->page!=V_PROVISION
        ? 1U|((unsigned)s->agent_alert.active.kind<<1)|((unsigned)s->agent_alert.active.agent<<4) : 0;
    if (agent_popup_state!=next) {
        agent_popup_state=next;
        lv_obj_invalidate_area(screen,&agent_popup_area);
    }
}
