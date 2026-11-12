#include "hosts.h"
#include <string.h>

bool vibe_host_hex(const char *v,size_t n) {
    if (!v || strlen(v)!=n) return false;
    for (size_t i=0;i<n;i++) if (!strchr("0123456789abcdef",v[i])) return false;
    return true;
}
bool vibe_route_equal(vibe_route_t a,vibe_route_t b) {
    if (a.kind!=b.kind || a.kind==V_LINK_NONE) return false;
    return a.kind==V_LINK_WIFI ? a.address==b.address && a.port==b.port : a.handle==b.handle && a.serial==b.serial;
}
static bool valid_name(const char *v) {
    if (!v || !v[0] || strlen(v)>=64) return false;
    for (const unsigned char *p=(const unsigned char *)v;*p;p++) if (*p<32 || *p==127) return false;
    return true;
}
bool vibe_hosts_decode(vibe_hosts_saved_t *out,const void *bytes,size_t size) {
    if (size!=sizeof(*out)) return false;
    vibe_hosts_saved_t next;memcpy(&next,bytes,size);
    if (next.version!=1 || next.count>VIBE_HOSTS_MAX) return false;
    for (unsigned i=0;i<next.count;i++) {
        vibe_host_t *p=&next.hosts[i];
        if (!memchr(p->id,0,sizeof(p->id)) || !memchr(p->token,0,sizeof(p->token)) ||
            !memchr(p->name,0,sizeof(p->name)) || !vibe_host_hex(p->token,64) ||
            (p->id[0] && !vibe_host_hex(p->id,32)) || !valid_name(p->name)) return false;
        for (unsigned j=0;j<i;j++) if (!strcmp(p->token,next.hosts[j].token) ||
            (p->id[0] && !strcmp(p->id,next.hosts[j].id))) return false;
    }
    *out=next;return true;
}
void vibe_hosts_init(vibe_hosts_t *h,const vibe_hosts_saved_t *saved,const char *legacy) {
    memset(h,0,sizeof(*h));h->owner=-1;h->saved.version=1;
    if (saved) h->saved=*saved;
    else if (vibe_host_hex(legacy,64)) {
        h->saved.count=1;strcpy(h->saved.hosts[0].token,legacy);strcpy(h->saved.hosts[0].name,"Computer");
    }
}
int vibe_hosts_auth(const vibe_hosts_t *h,const char *id,const char *token) {
    if (!vibe_host_hex(id,32) || !vibe_host_hex(token,64)) return -1;
    for (unsigned i=0;i<h->saved.count;i++) {
        const vibe_host_t *p=&h->saved.hosts[i];
        if (!strcmp(token,p->token) && (!p->id[0] || !strcmp(id,p->id))) return (int)i;
    }
    return -1;
}
static int update(vibe_hosts_saved_t *next,const char *id,const char *name,const char *token,int auth,bool repair) {
    if (!vibe_host_hex(id,32) || !vibe_host_hex(token,64) || !valid_name(name)) return -1;
    int slot=auth;
    for (unsigned i=0;i<next->count;i++) if ((int)i!=slot &&
        (!strcmp(next->hosts[i].id,id) || !strcmp(next->hosts[i].token,token))) return -1;
    if (slot<0) {
        if (next->count>=VIBE_HOSTS_MAX) return -1;
        slot=(int)next->count++;
    } else if ((unsigned)slot>=next->count || (!repair && strcmp(next->hosts[slot].token,token))) return -1;
    vibe_host_t *p=&next->hosts[slot];memset(p,0,sizeof(*p));strcpy(p->id,id);strcpy(p->name,name);strcpy(p->token,token);
    return slot;
}
int vibe_hosts_register(vibe_hosts_saved_t *next,const char *id,const char *name,const char *token,int auth) {
    return update(next,id,name,token,auth,false);
}
int vibe_hosts_pair(vibe_hosts_saved_t *next,const char *id,const char *name,const char *token) {
    int slot=-1;
    if (!id) return -1;
    for (unsigned i=0;i<next->count;i++) if (!strcmp(next->hosts[i].id,id)) { slot=(int)i;break; }
    return update(next,id,name,token,slot,true);
}
static bool alive(vibe_host_link_t l,uint64_t now) { return l.route.kind && now>=l.seen && now-l.seen<l.timeout_ms; }
static vibe_link_t preferred(const vibe_hosts_t *h,int host,uint64_t now) {
    if (host<0 || (unsigned)host>=h->saved.count) return V_LINK_NONE;
    if (alive(h->links[host][0],now)) return V_LINK_WIFI;
    return alive(h->links[host][1],now)?V_LINK_BLE:V_LINK_NONE;
}
void vibe_hosts_poll(vibe_hosts_t *h,uint64_t now) {
    if (h->owner>=0 && h->selected && alive(h->links[h->owner][h->selected-1],now)) return;
    /* Same owner's other route always wins over another computer. */
    vibe_link_t next=preferred(h,h->owner,now);
    int owner=h->owner;
    if (!next) {
        owner=-1;
        for (unsigned i=0;i<h->saved.count;i++) if ((next=preferred(h,(int)i,now))) { owner=(int)i;break; }
    }
    if (owner!=h->owner || next!=h->selected) { h->owner=owner;h->selected=next;++h->lease; }
}
void vibe_hosts_seen(vibe_hosts_t *h,int host,vibe_route_t route,uint64_t now,unsigned timeout,bool audio,bool hotkey) {
    if (host<0 || (unsigned)host>=h->saved.count || route.kind<V_LINK_WIFI || route.kind>V_LINK_BLE) return;
    vibe_host_link_t *l=&h->links[host][route.kind-1];
    /* An endpoint replacement is a fresh route; never retain pending controls. */
    if (h->owner==host && h->selected==route.kind && !vibe_route_equal(l->route,route)) ++h->lease;
    *l=(vibe_host_link_t){.route=route,.seen=now,.timeout_ms=timeout,.audio=audio,.hotkey=hotkey};
    vibe_hosts_poll(h,now);
}
void vibe_hosts_lost(vibe_hosts_t *h,vibe_route_t route) {
    for (unsigned i=0;i<h->saved.count;i++) for (unsigned j=0;j<2;j++)
        if (vibe_route_equal(h->links[i][j].route,route)) h->links[i][j]=(vibe_host_link_t){0};
}
void vibe_hosts_disable(vibe_hosts_t *h,vibe_link_t kind) {
    if (kind<V_LINK_WIFI || kind>V_LINK_BLE) return;
    for (unsigned i=0;i<h->saved.count;i++) h->links[i][kind-1]=(vibe_host_link_t){0};
}
bool vibe_hosts_claim(vibe_hosts_t *h,int host,vibe_route_t via,uint64_t now) {
    if (host<0 || (unsigned)host>=h->saved.count || via.kind<V_LINK_WIFI || via.kind>V_LINK_BLE ||
        !alive(h->links[host][via.kind-1],now) || !vibe_route_equal(via,h->links[host][via.kind-1].route)) return false;
    if (h->owner==host) return true; /* A duplicate claim must not switch routes. */
    h->owner=host;h->selected=preferred(h,host,now);++h->lease;return true;
}
vibe_host_link_t vibe_hosts_active(const vibe_hosts_t *h,uint64_t now) {
    if (h->owner<0 || !h->selected) return (vibe_host_link_t){0};
    vibe_host_link_t l=h->links[h->owner][h->selected-1];return alive(l,now)?l:(vibe_host_link_t){0};
}
bool vibe_hosts_controls(const vibe_hosts_t *h,int host,vibe_route_t route,uint64_t now) {
    return host==h->owner && vibe_route_equal(vibe_hosts_active(h,now).route,route);
}
