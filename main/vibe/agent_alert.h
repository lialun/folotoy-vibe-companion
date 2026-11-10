#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* No heap, timers, sockets or task stacks. Controller owns this state. */
typedef struct { uint64_t id,expires; uint8_t agent,kind; } vibe_agent_item_t;
typedef struct {
    vibe_agent_item_t items[4],active;
    uint64_t seen[16],visible_until;
    unsigned count,seen_next;
    bool visible;
} vibe_agent_alert_t;
bool vibe_agent_push(vibe_agent_alert_t *s,uint64_t id,unsigned agent,unsigned kind,uint64_t now,unsigned ttl);
bool vibe_agent_tick(vibe_agent_alert_t *s,uint64_t now,bool blocked);
/* A physical press closes the current notice and its already-queued backlog.
 * Keep deduplication IDs; the same packet must not make the notice reappear. */
bool vibe_agent_dismiss(vibe_agent_alert_t *s);
const char *vibe_agent_name(unsigned agent);
const char *vibe_agent_action(unsigned kind);
uint64_t vibe_agent_hash(const char *id);
#ifdef __cplusplus
}
#endif
