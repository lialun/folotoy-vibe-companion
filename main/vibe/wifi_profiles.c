#include "wifi_profiles.h"
#include <string.h>

static size_t length(const char *s,size_t capacity) {
    const char *end=memchr(s,0,capacity);return end?(size_t)(end-s):capacity;
}
bool vibe_wifi_credential_valid(const vibe_wifi_credential_t *e) {
    if (!e) return false;
    size_t ssid=length(e->ssid,sizeof(e->ssid)),password=length(e->password,sizeof(e->password));
    return ssid>0 && ssid<=32 && password<=63 && (!password || password>=8);
}
int vibe_wifi_profiles_find(const vibe_wifi_profiles_t *p,const char *ssid) {
    if (!p || !ssid || p->count>VIBE_WIFI_PROFILE_MAX) return -1;
    for (unsigned i=0;i<p->count;i++) if (!strcmp(p->entries[i].ssid,ssid)) return (int)i;
    return -1;
}
bool vibe_wifi_profiles_put(vibe_wifi_profiles_t *p,const vibe_wifi_credential_t *e) {
    if (!p || p->count>VIBE_WIFI_PROFILE_MAX || !vibe_wifi_credential_valid(e)) return false;
    int found=vibe_wifi_profiles_find(p,e->ssid);
    if (found<0) { if (p->count==VIBE_WIFI_PROFILE_MAX) return false;found=p->count++; }
    vibe_wifi_credential_t copy=*e;e=&copy;
    memset(&p->entries[found],0,sizeof(p->entries[found]));
    memcpy(p->entries[found].ssid,e->ssid,strlen(e->ssid));
    memcpy(p->entries[found].password,e->password,strlen(e->password));
    p->preferred=(uint8_t)found;return true;
}
bool vibe_wifi_profiles_remove(vibe_wifi_profiles_t *p,const char *ssid) {
    int found=vibe_wifi_profiles_find(p,ssid);if (found<0) return false;
    memmove(&p->entries[found],&p->entries[found+1],(p->count-(unsigned)found-1)*sizeof(p->entries[0]));
    p->count--;memset(&p->entries[p->count],0,sizeof(p->entries[0]));
    if (!p->count || p->preferred==(unsigned)found) p->preferred=0;
    else if (p->preferred>(unsigned)found) p->preferred--;
    return true;
}
size_t vibe_wifi_profiles_encode(const vibe_wifi_profiles_t *p,void *blob,size_t capacity) {
    if (!p || !blob || p->count>VIBE_WIFI_PROFILE_MAX || (p->count?p->preferred>=p->count:p->preferred!=0) || capacity<6) return 0;
    uint8_t *out=blob;memcpy(out,"VWP1",4);out[4]=p->count;out[5]=p->preferred;size_t used=6;
    for (unsigned i=0;i<p->count;i++) {
        const vibe_wifi_credential_t *e=&p->entries[i];
        if (!vibe_wifi_credential_valid(e)) return 0;
        for (unsigned j=0;j<i;j++) if (!strcmp(p->entries[j].ssid,e->ssid)) return 0;
        size_t ssid=strlen(e->ssid),password=strlen(e->password);
        if (capacity-used<2+ssid+password) return 0;
        out[used++]=(uint8_t)ssid;out[used++]=(uint8_t)password;
        memcpy(out+used,e->ssid,ssid);used+=ssid;memcpy(out+used,e->password,password);used+=password;
    }
    return used;
}
bool vibe_wifi_profiles_decode(vibe_wifi_profiles_t *p,const void *blob,size_t size) {
    if (!p || !blob || size<6 || size>VIBE_WIFI_BLOB_MAX) return false;
    const uint8_t *in=blob;
    if (memcmp(in,"VWP1",4) || in[4]>VIBE_WIFI_PROFILE_MAX || (in[4]?in[5]>=in[4]:in[5]!=0)) return false;
    vibe_wifi_profiles_t next={0};next.count=in[4];next.preferred=in[5];size_t used=6;
    for (unsigned i=0;i<next.count;i++) {
        if (size-used<2) return false;
        size_t ssid=in[used++],password=in[used++];
        if (!ssid || ssid>32 || password>63 || (password && password<8) || size-used<ssid+password) return false;
        if (memchr(in+used,0,ssid+password)) return false;
        memcpy(next.entries[i].ssid,in+used,ssid);used+=ssid;
        memcpy(next.entries[i].password,in+used,password);used+=password;
        for (unsigned j=0;j<i;j++) if (!strcmp(next.entries[j].ssid,next.entries[i].ssid)) return false;
    }
    if (used!=size) return false;
    *p=next;return true;
}
bool vibe_wifi_profiles_commit(vibe_wifi_profiles_t *current,const vibe_wifi_profiles_t *next,
                              vibe_wifi_persist_fn persist,void *context) {
    uint8_t blob[VIBE_WIFI_BLOB_MAX];size_t size=vibe_wifi_profiles_encode(next,blob,sizeof(blob));
    if (!current || !size || !persist) return false;
    if (!memcmp(current,next,sizeof(*current))) { memset(blob,0,sizeof(blob));return true; }
    bool stored=persist(blob,size,context);memset(blob,0,sizeof(blob));
    if (stored) *current=*next;
    return stored;
}
unsigned vibe_wifi_retry_index(unsigned count,unsigned preferred,unsigned attempt) {
    return count && count<=VIBE_WIFI_PROFILE_MAX && preferred<count?(preferred+attempt%count)%count:0;
}
unsigned vibe_wifi_retry_delay_ms(unsigned failed_rounds) {
    return failed_rounds>=4?60000:5000u<<failed_rounds;
}
