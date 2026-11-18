/* Generic data layouts, independent of Agent names and provider endpoints. */
static unsigned quota_rows;
static lv_obj_t *quota_bars[3],*quota_fills[3],*quota_counter;
static const char *quota_fit(const char *source,char *out,unsigned capacity,int width,const lv_font_t *font) {
    lv_point_t size;lv_text_get_size(&size,source,font,-1,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    if(size.x<=width)return source;
    snprintf(out,capacity,"%s",source);
    size_t length=strlen(out);
    while(length) {
        do {length--;} while(length && ((unsigned char)out[length]&0xc0)==0x80);
        out[length]=0;char candidate[96];snprintf(candidate,sizeof(candidate),"%.90s...",out);
        lv_text_get_size(&size,candidate,font,-1,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        if(size.x<=width){snprintf(out,capacity,"%s",candidate);return out;}
    }
    snprintf(out,capacity,"...");return out;
}
static void quota_create(void) {
    quota_counter=label(module_root,168,64,44,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(quota_counter,LV_TEXT_ALIGN_RIGHT,0);
    for (unsigned i=0;i<quota_rows;i++) {
        quota_bars[i]=box(module_root,8,160,204,4,PANEL,0);
        quota_fills[i]=box(quota_bars[i],0,0,204,4,graphic_color(ACCENT),0);
    }
}
static void quota_update(const vibe_quota_card_t *card,unsigned part,unsigned parts,uint64_t now) {
    char text[96],label_buffer[40];
    lv_point_t size;lv_text_get_size(&size,card->name,&body,-1,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    int heading_width=parts>1?156:204;
    const lv_font_t *heading_font=size.x>heading_width?&small:&body;
    if(lv_obj_get_style_text_font(module_heading,0)!=heading_font)lv_obj_set_style_text_font(module_heading,heading_font,0);
    if(lv_obj_get_width(module_heading)!=heading_width)lv_obj_set_width(module_heading,heading_width);
    char fitted[96];put(module_heading,quota_fit(card->name,fitted,sizeof(fitted),heading_width,heading_font));
    show(quota_counter,parts>1);
    if(parts>1){snprintf(text,sizeof(text),"%u/%u",part,parts);put(quota_counter,text);}
    bool single=card->count==1;
    const vibe_quota_metric_t *first=&card->metrics[0];
    home_line(0,8,112,204,&small,MUTED,first->kind==1?"剩余额度":"账户余额");
    show(module_value,single);
    for(unsigned i=0;i<quota_rows;i++) {
        bool present=i<card->count;
        if(!present){show(quota_bars[i],false);continue;}
        const vibe_quota_metric_t *m=&card->metrics[i];
        unsigned y=single?146:card->count==3?134+i*58:140+i*92;
        const char *name=m->kind==1?vibe_quota_label(m,label_buffer,sizeof(label_buffer)):
            !strcmp(m->currency,"CNY")?"人民币":!strcmp(m->currency,"USD")?"美元":m->currency;
        home_line(1+i*3,8,(int)y,single?204:122,&small,INK,quota_fit(name,fitted,sizeof(fitted),single?204:122,&small));
        if(m->kind==1) {
            if(m->remaining && m->remaining<10)strcpy(text,"<0.1%");
            else if(m->remaining%100)snprintf(text,sizeof(text),"%.1f%%",m->remaining/100.0);
            else snprintf(text,sizeof(text),"%u%%",(unsigned)m->remaining/100);
        }
        else {char compact[24];snprintf(text,sizeof(text),"%s",vibe_quota_amount(m->amount,compact,sizeof(compact)));}
        if(single) {
            if(lv_obj_get_y(module_value)!=174-HOME_TOP)lv_obj_set_y(module_value,174-HOME_TOP);
            const lv_font_t *font=m->kind==1?&lv_font_montserrat_48:&lv_font_montserrat_28;
            if(lv_obj_get_style_text_font(module_value,0)!=font)lv_obj_set_style_text_font(module_value,font,0);
            text_color(module_value,INK);put(module_value,text);
        } else home_line(2+i*3,130,(int)y-4,82,m->kind==1?&numbers20:&numbers16,INK,text);
        show(quota_bars[i],m->kind==1);
        unsigned bar_y=single?240:card->count==3?y+23:y+25;
        if(m->kind==1) {
            if(lv_obj_get_y(quota_bars[i])!=(int)bar_y-HOME_TOP)lv_obj_set_y(quota_bars[i],(int)bar_y-HOME_TOP);
            unsigned width=(204U*m->remaining+5000)/10000;
            show(quota_fills[i],width>0);if(width && lv_obj_get_width(quota_fills[i])!=(int)width)lv_obj_set_width(quota_fills[i],width);
            tint(quota_fills[i],graphic_color(m->remaining<=1000?FOCUS_COLOR:ACCENT));
        }
        vibe_quota_reset_text(m->reset_ms,now,text,sizeof(text));
        if(text[0])home_line(3+i*3,8,(int)(single?259:card->count==3?bar_y+6:bar_y+12),204,&small,MUTED,text);
    }
    for(unsigned i=0;i<module_count;i++) {
        const lv_font_t *font=lv_obj_get_style_text_font(module_text[i],0);
        if(lv_obj_get_height(module_text[i])!=font->line_height)lv_obj_set_height(module_text[i],font->line_height);
        show(module_text[i],(home_lines&(1U<<i))!=0);
    }
}
