#pragma once
#include "ble_frame.h"
#include "ble_media.h"
#include "ble_parameters.h"
#define VIBE_BLE_LINK_SLOTS 2
#define VIBE_BLE_NO_HANDLE UINT16_MAX
typedef struct { uint16_t handle;uint32_t generation;bool subscribed;vibe_ble_frame_t incoming;vibe_ble_media_t media;vibe_ble_parameters_t parameters; } vibe_ble_link_t;
typedef struct { vibe_ble_link_t links[VIBE_BLE_LINK_SLOTS];uint32_t generation; } vibe_ble_links_t;
static inline void vibe_ble_links_init(vibe_ble_links_t *s) {
    memset(s,0,sizeof(*s));for (unsigned i=0;i<VIBE_BLE_LINK_SLOTS;i++) s->links[i].handle=VIBE_BLE_NO_HANDLE;
}
static inline vibe_ble_link_t *vibe_ble_link_find(vibe_ble_links_t *s,uint16_t handle) {
    if (handle==VIBE_BLE_NO_HANDLE) return NULL;
    for (unsigned i=0;i<VIBE_BLE_LINK_SLOTS;i++) if (s->links[i].handle==handle) return &s->links[i];
    return NULL;
}
static inline vibe_ble_link_t *vibe_ble_link_ensure(vibe_ble_links_t *s,uint16_t handle) {
    vibe_ble_link_t *link=vibe_ble_link_find(s,handle);if (link || handle==VIBE_BLE_NO_HANDLE) return link;
    for (unsigned i=0;i<VIBE_BLE_LINK_SLOTS;i++) if (s->links[i].handle==VIBE_BLE_NO_HANDLE) {
        link=&s->links[i];memset(link,0,sizeof(*link));link->handle=handle;link->generation=++s->generation;
        if (!link->generation) link->generation=++s->generation;
        return link;
    }
    return NULL;
}
static inline void vibe_ble_link_remove(vibe_ble_links_t *s,uint16_t handle) {
    vibe_ble_link_t *link=vibe_ble_link_find(s,handle);
    if (link) { vibe_ble_media_clear(&link->media);link->subscribed=false;link->handle=VIBE_BLE_NO_HANDLE; }
}
static inline void vibe_ble_link_subscription(vibe_ble_links_t *s,uint16_t handle,uint16_t attribute,uint16_t notify_attribute,bool enabled) {
    /* Generic GATT Service Changed indications have their own CCCD. Their
     * cur_notify=false must never disable our control/audio notifications. */
    if (!notify_attribute || attribute!=notify_attribute) return;
    /* A late unsubscribe after DISCONNECT must not resurrect the old handle. */
    vibe_ble_link_t *link=enabled?vibe_ble_link_ensure(s,handle):vibe_ble_link_find(s,handle);
    if (link) link->subscribed=enabled;
}
