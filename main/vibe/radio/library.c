#include "library.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
size_t vibe_station_list_bytes(unsigned count) {
    return count<=VIBE_STATION_MAX ? sizeof(vibe_station_list_t)+count*sizeof(vibe_station_t) : 0;
}
vibe_station_list_t *vibe_station_list_create(unsigned count) {
    size_t bytes=vibe_station_list_bytes(count);
    vibe_station_list_t *list=bytes?calloc(1,bytes):NULL;
    if (list) { list->version=1;list->revision=1;list->count=count; }
    return list;
}
size_t vibe_station_list_storage_bytes(const vibe_station_list_t *list) {
    if (!vibe_station_list_valid(list)) return 0;
    size_t bytes=3*sizeof(uint32_t);
    for (unsigned i=0;i<list->count;i++) bytes+=strlen(list->stations[i].name)+strlen(list->stations[i].url)+2;
    return bytes;
}
bool vibe_station_list_encode(const vibe_station_list_t *list,void *bytes,size_t size) {
    size_t required=vibe_station_list_storage_bytes(list);
    if (!bytes || !required || size!=required) return false;
    uint32_t header[3]={2,list->revision,list->count};
    memcpy(bytes,header,sizeof(header));
    char *out=(char *)bytes+sizeof(header);
    for (unsigned i=0;i<list->count;i++) {
        size_t n=strlen(list->stations[i].name)+1;memcpy(out,list->stations[i].name,n);out+=n;
        n=strlen(list->stations[i].url)+1;memcpy(out,list->stations[i].url,n);out+=n;
    }
    return true;
}
vibe_station_list_t *vibe_station_list_decode(const void *bytes,size_t size) {
    uint32_t header[3];
    if (!bytes || size<sizeof(header)) return NULL;
    memcpy(header,bytes,sizeof(header));
    size_t required=vibe_station_list_bytes(header[2]);
    const size_t legacy=sizeof(header)+16*sizeof(vibe_station_t);
    if (!header[1] || !required || size>vibe_station_list_bytes(VIBE_STATION_MAX)) return NULL;
    if (header[0]==1 && (size<required || (size!=required && size!=legacy))) return NULL;
    if (header[0]!=1 && header[0]!=2) return NULL;
    vibe_station_list_t *list=vibe_station_list_create(header[2]);
    if (list) {
        if (header[0]==1) memcpy(list,bytes,required);
        else {
            list->revision=header[1];
            const char *p=(const char *)bytes+sizeof(header),*end=(const char *)bytes+size;
            for (unsigned i=0;i<list->count;i++) {
                const char *zero=memchr(p,0,(size_t)(end-p));
                if (!zero || (size_t)(zero-p)>=sizeof(list->stations[i].name)) { free(list);return NULL; }
                memcpy(list->stations[i].name,p,(size_t)(zero-p)+1);p=zero+1;
                zero=memchr(p,0,(size_t)(end-p));
                if (!zero || (size_t)(zero-p)>=sizeof(list->stations[i].url)) { free(list);return NULL; }
                memcpy(list->stations[i].url,p,(size_t)(zero-p)+1);p=zero+1;
            }
            if (p!=end) { free(list);return NULL; }
        }
        if (!vibe_station_list_valid(list)) { free(list);list=NULL; }
    }
    return list;
}
void vibe_library_clear(vibe_library_t *l) {
    free(l->active);free(l->staged);memset(l,0,sizeof(*l));
}
static bool bounded(const char *s,size_t capacity) {
    const char *end=memchr(s,0,capacity);
    if (!end || end==s) return false;
    for (const char *p=s;p<end;p++) if ((unsigned char)*p<32 || (unsigned char)*p==127) return false;
    return true;
}
bool vibe_station_valid(const vibe_station_t *s) {
    if (!s || !bounded(s->name,sizeof(s->name)) || !bounded(s->url,sizeof(s->url))) return false;
    const char *host=!strncmp(s->url,"http://",7)?s->url+7:!strncmp(s->url,"https://",8)?s->url+8:NULL;
    if (!host || !*host || *host=='/' || *host=='?' || *host=='#') return false;
    const char *end=host+strcspn(host,"/?#");
    if (host==end || memchr(host,'@',end-host)) return false;
    for (const char *p=s->url;*p;p++) if ((unsigned char)*p<=32 || (unsigned char)*p>=127 || *p=='\\') return false;
    return true;
}
bool vibe_station_list_valid(const vibe_station_list_t *list) {
    if (!list || list->version!=1 || !list->revision || list->count>VIBE_STATION_MAX) return false;
    for (unsigned i=0;i<list->count;i++) {
        if (!vibe_station_valid(&list->stations[i])) return false;
        for (unsigned j=0;j<i;j++) if (!strcmp(list->stations[i].url,list->stations[j].url)) return false;
    }
    return true;
}
static void discard(vibe_library_t *l) { free(l->staged);l->staged=NULL;l->received=0;l->transaction[0]=0; }
void vibe_library_abort(vibe_library_t *l) { discard(l); }
void vibe_library_expire(vibe_library_t *l,uint64_t now) { if (l->staged && now>=l->expires) discard(l); }
static bool integer(const cJSON *o,const char *key,uint32_t low,uint32_t high,uint32_t *value) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || floor(j->valuedouble)!=j->valuedouble || j->valuedouble<low || j->valuedouble>high) return false;
    *value=(uint32_t)j->valuedouble;return true;
}
static const char *string(const cJSON *o,const char *key) {
    const cJSON *j=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(j)?j->valuestring:"";
}
static bool transaction_id(const char *s) {
    size_t n=strlen(s);if (!n || n>36) return false;
    for (size_t i=0;i<n;i++) if (!strchr("0123456789abcdefABCDEF-",s[i])) return false;
    return true;
}
cJSON *vibe_library_handle(vibe_library_t *l,const cJSON *request,uint64_t now,vibe_library_save_t save,void *context) {
    vibe_library_expire(l,now);
    const char *type=string(request,"type"),*tid=string(request,"transaction"),*error=NULL;
    uint32_t index=0,count=0,revision=0;
    cJSON *reply=cJSON_CreateObject();if (!reply) return NULL;
    cJSON_AddStringToObject(reply,"event","radio.reply");cJSON_AddStringToObject(reply,"id",string(request,"id"));
    if (!l->active) error="设备电台列表暂不可用";
    else if (!strcmp(type,"radio.get")) {
        if (!l->active->count) { /* Empty lists have a valid header and no station. */ }
        else if (!integer(request,"index",0,l->active->count-1,&index)) error="电台索引无效";
        else if (cJSON_HasObjectItem(request,"revision") && (!integer(request,"revision",1,UINT32_MAX,&revision) || revision!=l->active->revision)) error="列表已更改，请重新读取";
        else {
            cJSON_AddNumberToObject(reply,"index",index);
            cJSON *station=cJSON_AddObjectToObject(reply,"station");
            cJSON_AddStringToObject(station,"name",l->active->stations[index].name);
            cJSON_AddStringToObject(station,"url",l->active->stations[index].url);
        }
    } else if (!transaction_id(tid)) error="同步标识无效";
    else if (!strcmp(type,"radio.begin")) {
        if (!integer(request,"count",0,VIBE_STATION_MAX,&count) || !integer(request,"revision",1,UINT32_MAX,&revision) || revision!=l->active->revision) error="列表已更改，请重新读取";
        else if (l->staged && !strcmp(tid,l->transaction) && l->staged->count==count) { /* Lost begin reply: retain received entries. */ }
        else if (l->staged) error="另一次电台同步尚未完成";
        else {
            l->staged=vibe_station_list_create(count);
            if (!l->staged) error="设备内存不足，请稍后重试";
            else { l->staged->version=1;l->staged->revision=revision;l->staged->count=count;strcpy(l->transaction,tid);l->expires=now+60000; }
        }
    } else if (!strcmp(type,"radio.commit") && !strcmp(tid,l->committed)) { /* Idempotent retry after lost commit reply. */ }
    else if (!l->staged || strcmp(tid,l->transaction)) error="同步已过期，请重试";
    else if (!strcmp(type,"radio.put")) {
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(request,"station");
        const char *name=string(item,"name"),*url=string(item,"url");vibe_station_t station={0};
        if (!l->staged->count || !integer(request,"index",0,l->staged->count-1,&index) || strlen(name)>=sizeof(station.name) || strlen(url)>=sizeof(station.url)) error="电台格式无效";
        else {
            strcpy(station.name,name);strcpy(station.url,url);
            if (!vibe_station_valid(&station)) error="电台名称或地址无效";
            else { l->staged->stations[index]=station;l->received|=UINT64_C(1)<<index;l->expires=now+60000; }
        }
    } else if (!strcmp(type,"radio.commit")) {
        if (l->received!=(UINT64_C(1)<<l->staged->count)-1 || !vibe_station_list_valid(l->staged)) error="列表未收齐或电台重复";
        else {
            bool changed=l->active->count!=l->staged->count || memcmp(l->active->stations,l->staged->stations,l->staged->count*sizeof(vibe_station_t));
            l->staged->revision=changed?(l->active->revision==UINT32_MAX?1:l->active->revision+1):l->active->revision;
            if (changed && (!save || !save(l->staged,context))) error="电台列表保存失败，请重试";
            else {
                if (changed) { free(l->active);l->active=l->staged;l->staged=NULL; }
                strcpy(l->committed,tid);discard(l);
            }
        }
    } else if (!strcmp(type,"radio.abort")) discard(l);
    else error="未知电台操作";
    cJSON_AddBoolToObject(reply,"ok",!error);
    cJSON_AddNumberToObject(reply,"revision",l->active?l->active->revision:0);cJSON_AddNumberToObject(reply,"count",l->active?l->active->count:0);
    cJSON_AddNumberToObject(reply,"capacity",VIBE_STATION_MAX);
    if (error) cJSON_AddStringToObject(reply,"error",error);
    return reply;
}
