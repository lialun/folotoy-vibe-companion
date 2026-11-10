/* Private weather view; included after the shared UI helpers in ui.c. */
static lv_obj_t *weather_counter,*forecast_images[3],*hour_graph;
typedef struct {
    int y[6],height[6];bool temperature[6],rain[6];
    char hours[6][8],temperatures[6][8],probabilities[6][8],amounts[6][8];
} weather_graph_t;
static weather_graph_t graph_data;
static uint32_t weather_uv_color(vibe_uv_level_t level) {
    if (level==V_UV_LOW) return ACCENT;
    if (level==V_UV_MODERATE) return AMBER;
    if (level==V_UV_HIGH) return FOCUS_COLOR;
    if (level==V_UV_VERY_HIGH) return palette==&dark_palette?0xFF91A4:0x8D233A;
    return palette==&dark_palette?0xD8AFFA:0x58317D;
}
static const char *weather_brief(int code) {
    if (code==96 || code==99) return "雷暴冰雹";
    if (code==56 || code==57) return "冻毛毛雨";
    if (code>=51 && code<=55) return "毛毛雨";
    if (code==66 || code==67) return "冻雨";
    return vibe_weather_text(code);
}
static void graph_line(lv_layer_t *layer,const lv_area_t *area,int x1,int y1,int x2,int y2,uint32_t color,int width) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.p1=(lv_point_precise_t){area->x1+x1-8,area->y1+y1-108};
    d.p2=(lv_point_precise_t){area->x1+x2-8,area->y1+y2-108};
    d.color=lv_color_hex(color);d.width=width;d.round_start=d.round_end=1;
    lv_draw_line(layer,&d);
}
static void graph_rect(lv_layer_t *layer,const lv_area_t *area,int x,int y,int w,int h,uint32_t color,int radius) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.radius=radius;
    lv_area_t a={area->x1+x-8,area->y1+y-108,area->x1+x+w-9,area->y1+y+h-109};
    lv_draw_rect(layer,&d,&a);
}
static void graph_text(lv_layer_t *layer,const lv_area_t *area,unsigned i,int y,const char *text,uint32_t color) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=&numbers16;d.color=lv_color_hex(color);
    d.text=text;d.text_local=1;d.align=LV_TEXT_ALIGN_CENTER;d.letter_space=-1;
    lv_area_t a={area->x1+(int)i*34,area->y1+y-108,area->x1+(int)i*34+33,area->y1+y+19-108};
#ifdef VIBE_UI_PREVIEW
    lv_point_t size;lv_text_get_size(&size,text,&numbers16,-1,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(size.x<=34 && size.y<=22 && a.x1>=area->x1 && a.x2<=area->x2 && a.y1>=area->y1 && a.y2<=area->y2);
#endif
    lv_draw_label(layer,&d,&a);
}
static void draw_hour_graph(lv_event_t *event) {
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t area;lv_obj_get_coords(lv_event_get_target_obj(event),&area);
    graph_line(layer,&area,8,199,212,199,EDGE,1);
    for (unsigned i=0;i<6;i++) {
        int x=25+(int)i*34;
        graph_text(layer,&area,i,114,graph_data.hours[i],MUTED);
        graph_text(layer,&area,i,graph_data.y[i]-20,graph_data.temperatures[i],INK);
        graph_text(layer,&area,i,230,graph_data.probabilities[i],INK);
        graph_text(layer,&area,i,310-graph_data.height[i]-19,graph_data.amounts[i],INK);
        if (i && graph_data.temperature[i-1] && graph_data.temperature[i])
            graph_line(layer,&area,x-34,graph_data.y[i-1],x,graph_data.y[i],graphic_color(FOCUS_COLOR),2);
        if (graph_data.temperature[i]) graph_rect(layer,&area,x-2,graph_data.y[i]-2,5,5,graphic_color(FOCUS_COLOR),2);
        if (graph_data.rain[i]) {
            int h=graph_data.height[i];
            graph_rect(layer,&area,x-5,310-(h?h:1),11,h?h:1,h?graphic_color(REST_COLOR):EDGE,0);
        }
    }
}
static void weather_create(unsigned wp) {
    weather_counter=label(module_root,176,61,36,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(weather_counter,LV_TEXT_ALIGN_RIGHT,0);
    show(module_value,wp==0 || wp==2);
    lv_obj_set_style_text_font(module_value,&numbers36,0);
    lv_obj_set_width(module_value,100);
    if (wp==0 || wp==3) {
        unsigned count=wp==0?1:3;
        for (unsigned i=0;i<count;i++) {
            forecast_images[i]=lv_image_create(module_root);
            lv_obj_set_style_image_recolor_opa(forecast_images[i],LV_OPA_COVER,0);
            lv_obj_set_pos(forecast_images[i],160,wp==0?109-HOME_TOP:122-HOME_TOP+(int)i*66);
        }
    }
    if (wp==1) {
        memset(&graph_data,0,sizeof(graph_data));
        hour_graph=box(module_root,8,108-HOME_TOP,204,204,BG,0);
        lv_obj_add_event_cb(hour_graph,draw_hour_graph,LV_EVENT_DRAW_MAIN,NULL);
        /* Under labels, so numeric values always remain legible. */
        lv_obj_move_to_index(hour_graph,0);
        box(module_root,129,92-HOME_TOP,8,2,graphic_color(FOCUS_COLOR),0);
    }
}
static void weather_icon_set(unsigned i,int code,bool day) {
    const lv_image_dsc_t *icon=weather_icons[vibe_weather_icon(code,day)];
    if (lv_image_get_src(forecast_images[i])!=icon) lv_image_set_src(forecast_images[i],icon);
    uint32_t color=graphic_color(code<=3?WEATHER_COLOR:REST_COLOR);
    if (!lv_color_eq(lv_obj_get_style_image_recolor(forecast_images[i],0),lv_color_hex(color)))
        lv_obj_set_style_image_recolor(forecast_images[i],lv_color_hex(color),0);
}
static void weather_update(unsigned wp,const vibe_prefs_t *p,const vibe_status_t *s,uint64_t now) {
    const vibe_weather_t *w=&s->forecast;char text[144];
    snprintf(text,sizeof(text),"%u/4",wp+1);put(weather_counter,text);
    if (lv_obj_get_width(module_heading)!=166) lv_obj_set_width(module_heading,166);
    text_color(module_heading,INK);if (wp==0) text_color(module_value,palette==&light_palette?INK:WEATHER_COLOR);
    lv_obj_set_y(module_value,(wp==2?141:112)-HOME_TOP);
    if (wp==0) {
        snprintf(text,sizeof(text),"%s · %s",p->city,weather_brief(w->code));
        lv_point_t size;lv_text_get_size(&size,text,&body,-1,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        const lv_font_t *font=size.x>166?&small:&body;
        if (lv_obj_get_style_text_font(module_heading,0)!=font) lv_obj_set_style_text_font(module_heading,font,0);
        if (lv_obj_get_height(module_heading)!=font->line_height) lv_obj_set_height(module_heading,font->line_height);
        put(module_heading,text);
        snprintf(text,sizeof(text),"%d°",w->temperature);put(module_value,text);weather_icon_set(0,w->code,w->is_day);
        snprintf(text,sizeof(text),"%d° / %d°",w->days[0].low,w->days[0].high);home_line(0,8,166,113,&numbers20,INK,text);
        snprintf(text,sizeof(text),"雨 %d%%",w->days[0].rain);home_line(1,135,169,77,&small,MUTED,text);
        home_line(2,8,202,100,&small,MUTED,"体感");home_line(3,113,202,99,&small,MUTED,"湿度");
        snprintf(text,sizeof(text),"%d°",w->feels);home_line(4,8,225,100,&numbers20,INK,text);
        snprintf(text,sizeof(text),"%d%%",w->humidity);home_line(5,113,225,99,&numbers20,INK,text);
        home_line(6,8,268,100,&small,MUTED,"UV 今日最高");
        uint32_t uv_color=MUTED;
        if (w->days[0].uv_valid) {
            vibe_uv_level_t level=vibe_weather_uv_level(w->days[0].uv_tenths);
            snprintf(text,sizeof(text),"%u.%u · %s",w->days[0].uv_tenths/10,w->days[0].uv_tenths%10,vibe_weather_uv_text(level));
            uv_color=weather_uv_color(level);
        } else strcpy(text,"—");
        home_line(7,110,264,102,&body,uv_color,text);
        snprintf(text,sizeof(text),"%s%u 分钟前更新",now-s->weather_at_ms>1800000?"上次 ":"",(unsigned)((now-s->weather_at_ms)/60000));
        home_line(8,8,291,204,&small,MUTED,text);
    } else if (wp==1) {
        put(module_heading,"未来6小时");home_line(2,143,85,26,&numbers16,MUTED,"°C");
        int64_t stamp=time(NULL);
        if (stamp<1700000000) stamp=w->sampled_at+(int64_t)((now-s->weather_at_ms)/1000);
        unsigned start=vibe_weather_next_hour(w,stamp);int temperatures[6]={0};weather_graph_t next={0};unsigned max_rain=10;
        for (unsigned i=0;i<6;i++) {
            const vibe_hour_t *h=start+i<w->hour_count?&w->hours[start+i]:NULL;
            if (h && (h->fields&V_HOUR_TEMP)) { temperatures[i]=h->temperature;next.temperature[i]=true; }
            if (h && (h->fields&V_HOUR_RAIN)) { next.rain[i]=true;if (h->rain_tenths>max_rain) max_rain=h->rain_tenths; }
        }
        vibe_weather_plot(temperatures,next.temperature,next.y);
        for (unsigned i=0;i<6;i++) {
            const vibe_hour_t *h=start+i<w->hour_count?&w->hours[start+i]:NULL;
            if (h) { time_t t=(time_t)(h->at+w->tz_seconds);struct tm tm;gmtime_r(&t,&tm);snprintf(text,sizeof(text),"%02d",tm.tm_hour); }
            else strcpy(text,"—");
            snprintf(next.hours[i],sizeof(next.hours[i]),"%.7s",text);
            if (next.temperature[i]) snprintf(text,sizeof(text),"%d",temperatures[i]);else strcpy(text,"—");
            snprintf(next.temperatures[i],sizeof(next.temperatures[i]),"%.7s",text);
            if (h && (h->fields&V_HOUR_PROB)) snprintf(text,sizeof(text),"%d",h->probability);else strcpy(text,"—");
            snprintf(next.probabilities[i],sizeof(next.probabilities[i]),"%.7s",text);
            if (next.rain[i]) {
                next.height[i]=(int)((h->rain_tenths*22+max_rain/2)/max_rain);
                if (h->rain_tenths<1000) snprintf(text,sizeof(text),"%u.%u",h->rain_tenths/10,h->rain_tenths%10);
                else snprintf(text,sizeof(text),"%u",(h->rain_tenths+5)/10);
            } else strcpy(text,"—");
            snprintf(next.amounts[i],sizeof(next.amounts[i]),"%.7s",text);
        }
        home_line(0,8,208,204,&small,MUTED,"降雨概率 %");home_line(1,8,252,204,&small,MUTED,"降水量 mm");
        if (memcmp(&next,&graph_data,sizeof(next))) { graph_data=next;lv_obj_invalidate(hour_graph); }
    } else if (wp==2) {
        put(module_heading,"空气与风力");bool fresh=s->air.valid && now-s->air_at_ms<=1800000;
        home_line(0,8,116,204,&small,MUTED,"美标 AQI");
        if (fresh && s->air.aqi_valid) { if (s->air.aqi>999) strcpy(text,"999+");else snprintf(text,sizeof(text),"%u",s->air.aqi); } else strcpy(text,"—");
        put(module_value,text);
        uint32_t color=!fresh || !s->air.aqi_valid?MUTED:s->air.aqi<=50?ACCENT:s->air.aqi<=100?AMBER:FOCUS_COLOR;
        text_color(module_value,color);
        home_line(1,108,151,104,&small,color,fresh && s->air.aqi_valid?vibe_air_text(s->air.aqi):"—");
        home_line(2,8,188,80,&small,MUTED,"PM2.5");
        if (fresh && s->air.pm25_valid) snprintf(text,sizeof(text),"%u.%u µg/m³",s->air.pm25_tenths/10,s->air.pm25_tenths%10);else strcpy(text,"—");
        home_line(3,92,188,120,&small,INK,text);
        home_line(4,8,229,139,&body,INK,vibe_weather_direction(w->direction));
        if (w->wind_valid) snprintf(text,sizeof(text),"%u 级",vibe_weather_wind_level(w->wind_ms));else strcpy(text,"—");
        home_line(5,151,229,61,&body,REST_COLOR,text);
        home_line(6,8,265,139,&small,MUTED,"当前阵风");
        if (w->gust_valid) snprintf(text,sizeof(text),"%u 级",vibe_weather_wind_level(w->gust_ms));else strcpy(text,"—");
        home_line(7,151,262,61,&body,INK,text);
        home_line(8,8,293,139,&small,MUTED,"今日最大");
        if (w->days[0].wind_valid) snprintf(text,sizeof(text),"%u 级",vibe_weather_wind_level(w->days[0].wind_ms));else strcpy(text,"—");
        home_line(9,151,290,61,&body,INK,text);
    } else {
        put(module_heading,"三日预报");const char *names[]={"今天","明天","后天"};
        for (unsigned i=0;i<3;i++) {
            int y=112+(int)i*66;const vibe_forecast_t *d=&w->days[i];
            home_line(i*4,8,y,42,&body,INK,names[i]);home_line(i*4+1,54,y+3,100,&small,INK,vibe_weather_text(d->code));
            snprintf(text,sizeof(text),"%d° / %d°",d->low,d->high);home_line(i*4+2,8,y+23,146,&numbers20,INK,text);
            if (d->wind_valid) snprintf(text,sizeof(text),"雨%d%% 最大风%u级",d->rain,vibe_weather_wind_level(d->wind_ms));
            else snprintf(text,sizeof(text),"雨%d%% 最大风—",d->rain);
            home_line(i*4+3,8,y+48,146,&small,MUTED,text);weather_icon_set(i,d->code,true);
        }
    }
}
