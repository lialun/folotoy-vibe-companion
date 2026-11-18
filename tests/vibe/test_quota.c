#include "quota.h"
#include "model.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cJSON *sample(void) {
    return cJSON_Parse("{\"schema\":1,\"agent\":\"codex\",\"name\":\"Codex\",\"owner\":\"account-hash\",\"sample\":\"one\",\"stamp\":1790000000000,\"age_ms\":0,\"ttl_ms\":930000,\"items\":[{\"kind\":1,\"label\":\"Weekly\",\"remaining\":4700,\"minutes\":10080,\"reset_ms\":600000}]} ");
}
int main(void) {
    vibe_quota_store_t s={0};cJSON *o=sample();
    assert(vibe_quota_accept(&s,o,1000));assert(vibe_quota_pages(&s,1000)==1);
    cJSON *clear=cJSON_Parse("{\"agent\":\"codex\",\"owner\":\"account-hash\",\"sample\":\"obsolete\"}");
    assert(!vibe_quota_clear(&s,clear));assert(vibe_quota_pages(&s,1000)==1);cJSON_Delete(clear);
    vibe_quota_card_t card;unsigned part,parts;char text[96];
    assert(vibe_quota_page(&s,1000,0,&card,&part,&parts));assert(card.count==1 && card.metrics[0].remaining==4700);
    assert(!strcmp(vibe_quota_label(&card.metrics[0],text,sizeof(text)),"每周"));
    assert(vibe_quota_accept(&s,o,300000));assert(s.cards[0].expires==931000); /* Duplicate must not extend lease or reset. */
    assert(vibe_quota_pages(&s,601000)==0); /* Reset earlier than cache expiry. */
    cJSON_SetValuestring(cJSON_GetObjectItem(o,"sample"),"new");
    cJSON_SetNumberValue(cJSON_GetObjectItem(o,"stamp"),1789999999999.0);
    assert(!vibe_quota_accept(&s,o,2000));
    cJSON_SetNumberValue(cJSON_GetObjectItem(o,"stamp"),1790000000001.0);
    cJSON_SetNumberValue(cJSON_GetObjectItem(o,"age_ms"),300000);
    assert(!vibe_quota_accept(&s,o,2000)); /* Age and remaining TTL must agree. */
    cJSON_SetNumberValue(cJSON_GetObjectItem(o,"ttl_ms"),630000);assert(vibe_quota_accept(&s,o,2000));
    cJSON *items=cJSON_GetObjectItem(o,"items"),*row=cJSON_GetArrayItem(items,0);
    cJSON_SetNumberValue(cJSON_GetObjectItem(row,"remaining"),10001);assert(!vibe_quota_accept(&s,o,2000));
    cJSON_SetNumberValue(cJSON_GetObjectItem(row,"remaining"),0);
    cJSON_SetValuestring(cJSON_GetObjectItem(o,"sample"),"zero");
    assert(vibe_quota_accept(&s,o,2000));assert(vibe_quota_pages(&s,2000)==1);
    cJSON_SetNumberValue(cJSON_GetObjectItem(row,"reset_ms"),0);
    for(unsigned i=0;i<3;i++)cJSON_AddItemToArray(items,cJSON_Duplicate(row,true));
    cJSON_SetValuestring(cJSON_GetObjectItem(o,"sample"),"four");
    assert(vibe_quota_accept(&s,o,2000));assert(vibe_quota_pages(&s,2000)==2);
    assert(vibe_quota_page(&s,2000,1,&card,&part,&parts) && part==2 && parts==2 && card.count==2);
    assert(vibe_quota_pages(&s,632000)==0);
    cJSON_DeleteItemFromObject(o,"items");cJSON_AddArrayToObject(o,"items");
    cJSON_SetValuestring(cJSON_GetObjectItem(o,"sample"),"clear");assert(vibe_quota_accept(&s,o,3000));assert(vibe_quota_pages(&s,3000)==0);
    cJSON_Delete(o);
    o=sample();items=cJSON_GetObjectItem(o,"items");cJSON_DeleteItemFromArray(items,0);
    cJSON_AddItemToArray(items,cJSON_Parse("{\"kind\":2,\"label\":\"Balance\",\"amount\":\"28.50\",\"currency\":\"CNY\",\"reset_ms\":0}"));
    cJSON_SetValuestring(cJSON_GetObjectItem(o,"agent"),"new-agent");cJSON_SetValuestring(cJSON_GetObjectItem(o,"name"),"New Agent");
    assert(vibe_quota_accept(&s,o,3000));assert(vibe_quota_page(&s,3000,0,&card,&part,&parts));assert(card.metrics[0].kind==2);
    cJSON_SetValuestring(cJSON_GetObjectItem(cJSON_GetArrayItem(items,0),"amount"),"NaN");assert(!vibe_quota_accept(&s,o,3000));
    cJSON_Delete(o);
    vibe_quota_reset_text(1000+59000,1000,text,sizeof(text));assert(!strcmp(text,"1分钟后重置"));
    vibe_quota_reset_text(1000,1000,text,sizeof(text));assert(!text[0]);
    assert(!strcmp(vibe_quota_amount("123456789.12",text,sizeof(text)),"~123.5M"));
    unsigned index;
    assert(vibe_quota_select(0,4,1,&index) && index==0);
    assert(!vibe_quota_select(1,4,1,&index) && index==0);
    assert(!vibe_quota_select(4,4,1,&index) && index==3);
    assert(vibe_quota_select(1,4,2,&index) && index==1);
    assert(!vibe_quota_select(2,4,2,&index) && index==0);
    vibe_model_t m;vibe_model_init(&m,0);m.info_pages=6;m.screen_seconds=0;
    vibe_model_tick(&m,7999);assert(m.weather_page==0);vibe_model_tick(&m,8000);assert(m.weather_page==1);
    vibe_model_key(&m,V_SETTINGS,V_CLICK,9000);assert(m.weather_page==2);
    vibe_model_tick(&m,16000);assert(m.weather_page==2);vibe_model_tick(&m,17000);assert(m.weather_page==3);
    m.info_blocked=true;vibe_model_tick(&m,30000);assert(m.weather_page==3);
    m.info_blocked=false;m.recording=true;vibe_model_tick(&m,40000);assert(m.weather_page==3);
    m.recording=false;m.screen_on=false;vibe_model_tick(&m,50000);assert(m.weather_page==3);
    m.screen_on=true;m.weather_page=5;vibe_model_tick(&m,58000);assert(m.weather_page==0);
    vibe_quota_store_t windows={0};vibe_quota_card_t *w=&windows.cards[0];
    w->expires=100000;w->count=3;
    for(unsigned i=0;i<3;i++)w->metrics[i]=(vibe_quota_metric_t){.kind=1,.minutes=i==0?300:i==1?10080:43200,.reset_ms=2000+i*1000,.remaining=i*100};
    assert(vibe_quota_pages(&windows,1000)==1);
    assert(vibe_quota_page(&windows,1000,0,&card,&part,&parts) && card.count==3 && parts==1);
    assert(!strcmp(vibe_quota_label(&card.metrics[2],text,sizeof(text)),"每月"));
    assert(vibe_quota_page(&windows,2000,0,&card,&part,&parts) && card.count==2);
    w->metrics[2].minutes=0;assert(vibe_quota_pages(&windows,1000)==2);
    puts("Quota protocol, expiry, reset, generic layouts and carousel policy: PASS");
}
