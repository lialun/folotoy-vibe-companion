#include "quota.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash(const char *text) {
    uint64_t h=14695981039346656037ULL;
    for (;*text;text++) { h^=(unsigned char)*text;h*=1099511628211ULL; }
    return h?h:1;
}
static bool ascii(const cJSON *o,const char *key,char *out,unsigned capacity) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);
    if (!cJSON_IsString(j) || !j->valuestring[0] || strlen(j->valuestring)>=capacity) return false;
    for (const unsigned char *s=(const unsigned char *)j->valuestring;*s;s++) if (*s<32 || *s>126) return false;
    strcpy(out,j->valuestring);return true;
}
static bool number(const cJSON *o,const char *key,uint64_t low,uint64_t high,uint64_t *value) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || floor(j->valuedouble)!=j->valuedouble ||
        j->valuedouble<(double)low || j->valuedouble>(double)high) return false;
    *value=(uint64_t)j->valuedouble;return true;
}
bool vibe_quota_accept(vibe_quota_store_t *store,const cJSON *o,uint64_t now) {
    vibe_quota_card_t next={0};char id[65],owner[65];uint64_t ttl,age,version;
    if (!number(o,"schema",1,1,&version) || !ascii(o,"agent",next.agent,sizeof(next.agent)) ||
        !ascii(o,"name",next.name,sizeof(next.name)) || !ascii(o,"sample",id,sizeof(id)) ||
        !ascii(o,"owner",owner,sizeof(owner)) || !number(o,"stamp",1,4102444800000ULL,&next.stamp) ||
        !number(o,"ttl_ms",1,V_QUOTA_TTL_MS,&ttl) || !number(o,"age_ms",0,V_QUOTA_TTL_MS-1,&age) ||
        ttl>V_QUOTA_TTL_MS-age) return false;
    next.sample=hash(id);next.owner=hash(owner);next.expires=now+ttl;
    const cJSON *items=cJSON_GetObjectItemCaseSensitive(o,"items");
    int count=cJSON_GetArraySize(items);
    if (!cJSON_IsArray(items) || count<0 || count>V_QUOTA_METRICS) return false;
    for (int i=0;i<count;i++) {
        const cJSON *item=cJSON_GetArrayItem(items,i);vibe_quota_metric_t *m=&next.metrics[i];uint64_t kind,reset;
        if (!ascii(item,"label",m->label,sizeof(m->label)) || !number(item,"kind",1,2,&kind) ||
            !number(item,"reset_ms",0,366ULL*86400000,&reset)) return false;
        m->kind=(uint8_t)kind;m->reset_ms=reset?now+reset:0;
        if (kind==1) {
            uint64_t remaining,minutes;
            if (!number(item,"remaining",0,10000,&remaining) || !number(item,"minutes",0,527040,&minutes)) return false;
            m->remaining=(uint16_t)remaining;m->minutes=(uint32_t)minutes;
        } else {
            if (!ascii(item,"amount",m->amount,sizeof(m->amount)) || !ascii(item,"currency",m->currency,sizeof(m->currency))) return false;
            unsigned digits=0,dots=0;
            for (const char *s=m->amount;*s;s++) { if (*s=='.') ++dots;else if (*s>='0' && *s<='9') ++digits;else return false; }
            if (!digits || dots>1 || m->amount[0]=='.' || m->amount[strlen(m->amount)-1]=='.') return false;
            for (const char *s=m->currency;*s;s++) if (*s<'A' || *s>'Z') return false;
        }
    }
    next.count=(unsigned)count;
    unsigned slot=0;uint64_t oldest=UINT64_MAX;
    for (unsigned i=0;i<V_QUOTA_SLOTS;i++) {
        vibe_quota_card_t *old=&store->cards[i];
        if (!strcmp(old->agent,next.agent)) {
            if (next.stamp<old->stamp) return false;
            if (next.sample==old->sample) return next.owner==old->owner; /* Retry cannot renew TTL. */
            slot=i;break;
        }
        if (old->expires<oldest) { oldest=old->expires;slot=i; }
    }
    store->cards[slot]=next;return true;
}
static bool live(const vibe_quota_card_t *card,const vibe_quota_metric_t *m,uint64_t now) {
    return card->expires>now && (!m->reset_ms || m->reset_ms>now);
}
bool vibe_quota_clear(vibe_quota_store_t *s,const cJSON *o) {
    char agent[17],id[65],owner[65];
    if(!ascii(o,"agent",agent,sizeof(agent)) || !ascii(o,"sample",id,sizeof(id)) || !ascii(o,"owner",owner,sizeof(owner)))return false;
    for(unsigned i=0;i<V_QUOTA_SLOTS;i++) {
        vibe_quota_card_t *c=&s->cards[i];
        if(!strcmp(c->agent,agent)) {
            if(c->sample!=hash(id) || c->owner!=hash(owner))return false;
            c->count=0;return true; /* A delayed clear cannot erase a newer sample. */
        }
    }
    return true;
}
static unsigned page_capacity(const vibe_quota_card_t *c,uint64_t now,unsigned count) {
    if(count!=3)return 2;
    for(unsigned i=0;i<c->count;i++)if(live(c,&c->metrics[i],now) && (c->metrics[i].kind!=1 || !c->metrics[i].minutes))return 2;
    return 3;
}
unsigned vibe_quota_pages(const vibe_quota_store_t *s,uint64_t now) {
    unsigned pages=0;
    for (unsigned i=0;i<V_QUOTA_SLOTS;i++) {
        unsigned count=0;for (unsigned j=0;j<s->cards[i].count;j++) count+=live(&s->cards[i],&s->cards[i].metrics[j],now);
        unsigned capacity=page_capacity(&s->cards[i],now,count);pages+=(count+capacity-1)/capacity;
    }
    return pages;
}
bool vibe_quota_select(unsigned selection,unsigned weather,unsigned quota,unsigned *index) {
    unsigned total=weather+quota;
    selection=total?selection%total:0;
    if(selection<quota){*index=selection;return true;}
    *index=selection-quota;return false;
}
bool vibe_quota_page(const vibe_quota_store_t *s,uint64_t now,unsigned page,vibe_quota_card_t *out,unsigned *part,unsigned *parts) {
    for (unsigned i=0;i<V_QUOTA_SLOTS;i++) {
        const vibe_quota_card_t *c=&s->cards[i];unsigned indices[V_QUOTA_METRICS],count=0;
        for (unsigned j=0;j<c->count;j++) if (live(c,&c->metrics[j],now)) indices[count++]=j;
        unsigned capacity=page_capacity(c,now,count),n=(count+capacity-1)/capacity;
        if (page>=n) { page-=n;continue; }
        *out=*c;out->count=0;
        for (unsigned j=page*capacity;j<count && j<page*capacity+capacity;j++) out->metrics[out->count++]=c->metrics[indices[j]];
        *part=page+1;*parts=n;return true;
    }
    return false;
}
const char *vibe_quota_label(const vibe_quota_metric_t *m,char *s,unsigned size) {
    if (m->minutes==43200) return "每月";
    if (m->minutes==10080) return "每周";
    if (m->minutes==1440) return "每日";
    if (m->minutes && m->minutes%60==0 && m->minutes<1440) { snprintf(s,size,"%u 小时",(unsigned)m->minutes/60);return s; }
    return m->label;
}
void vibe_quota_reset_text(uint64_t deadline,uint64_t now,char *s,unsigned size) {
    if (!deadline || deadline<=now) { s[0]=0;return; }
    uint64_t minutes=(deadline-now+59999)/60000;
    if (minutes>=1440) {
        if(minutes%1440/60)snprintf(s,size,"%u天%u小时后重置",(unsigned)(minutes/1440),(unsigned)(minutes%1440/60));
        else snprintf(s,size,"%u天后重置",(unsigned)(minutes/1440));
    } else if (minutes>=60) {
        if(minutes%60)snprintf(s,size,"%u小时%u分后重置",(unsigned)(minutes/60),(unsigned)(minutes%60));
        else snprintf(s,size,"%u小时后重置",(unsigned)(minutes/60));
    }
    else snprintf(s,size,"%u分钟后重置",(unsigned)minutes);
}
const char *vibe_quota_amount(const char *amount,char *s,unsigned size) {
    if(strlen(amount)<=8)return amount;
    double value=strtod(amount,NULL);
    if(value>=1000000000)snprintf(s,size,"~%.1fB",value/1000000000);
    else if(value>=1000000)snprintf(s,size,"~%.1fM",value/1000000);
    else snprintf(s,size,"~%.1fk",value/1000);
    return s;
}
