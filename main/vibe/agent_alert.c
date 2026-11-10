#include "agent_alert.h"
#include <string.h>
uint64_t vibe_agent_hash(const char *id) {
    uint64_t h=14695981039346656037ULL;
    for (;*id;id++) { h^=(unsigned char)*id;h*=1099511628211ULL; }
    return h?h:1;
}
const char *vibe_agent_name(unsigned agent) {
    static const char *names[]={"Codex","Claude","OpenCode","DSH","ZCode","Gemini","Qwen"};
    return agent<7?names[agent]:"Agent";
}
const char *vibe_agent_action(unsigned kind) {
    static const char *actions[]={"任务已完成","等待审批","等待回答","任务出错"};
    return kind<4?actions[kind]:"";
}
bool vibe_agent_push(vibe_agent_alert_t *s,uint64_t id,unsigned agent,unsigned kind,uint64_t now,unsigned ttl) {
    if (!id || agent>=7 || kind>=4 || !ttl || ttl>10000) return false;
    for (unsigned i=0;i<16;i++) if (s->seen[i]==id) return true;
    if (s->count==4) {
        /* Keep waiting-for-human events over an expendable completion. */
        unsigned victim=0;
        for (unsigned i=0;i<4;i++) if (s->items[i].kind==0) { victim=i;break; }
        memmove(&s->items[victim],&s->items[victim+1],(3-victim)*sizeof(s->items[0]));--s->count;
    }
    s->seen[s->seen_next++%16]=id;
    s->items[s->count++]=(vibe_agent_item_t){.id=id,.expires=now+ttl,.agent=agent,.kind=kind};
    return true;
}
bool vibe_agent_tick(vibe_agent_alert_t *s,uint64_t now,bool blocked) {
    if (s->visible && now>=s->visible_until) s->visible=false;
    /* Stale queued events never ring after a long voice recording or provisioning. */
    for (unsigned i=0;i<s->count;) {
        if (now>=s->items[i].expires) { --s->count;memmove(&s->items[i],&s->items[i+1],(s->count-i)*sizeof(s->items[0])); }
        else ++i;
    }
    /* A burst must not wait five seconds per event. Keep each notice readable
     * for two seconds, then allow the next fresh event to replace it. */
    if (!blocked && s->count && s->visible && now+3000>=s->visible_until) s->visible=false;
    if (blocked || s->visible || !s->count) return false;
    s->active=s->items[0];--s->count;memmove(s->items,s->items+1,s->count*sizeof(s->items[0]));
    s->visible=true;s->visible_until=now+5000;
    return true;
}
bool vibe_agent_dismiss(vibe_agent_alert_t *s) {
    if (!s->visible) return false;
    s->visible=false;s->visible_until=0;s->count=0;
    return true;
}
