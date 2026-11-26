#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIBE_JSON_MAX 1536
typedef enum { V_MSG_KEY, V_MSG_JSON, V_MSG_PROVISION, V_MSG_CONNECTION_CHANGED, V_MSG_AUDIO_FAIL } vibe_msg_type_t;
enum { V_KEY_SCREEN_VISIBLE=1U<<8 };
typedef struct {
    vibe_msg_type_t type;
    int key, event;
    /* Key events reuse JSON's generation storage and one event flag, retaining
     * the compact queue entry while capturing the visible screen at the edge. */
    union { uint32_t link_generation;uint32_t ui_frame; };
    /* Only JSON messages own a payload; key events do not reserve 1.5 KiB. */
    union { char *json; uint64_t key_at_ms; };
} vibe_msg_t;
typedef enum {V_DELIVERY_PRIMARY,V_DELIVERY_BOTH,V_DELIVERY_CRITICAL} vibe_delivery_t;
typedef struct {
    char *json;
    uint32_t owner;
    vibe_delivery_t delivery;
} vibe_outbound_t;
/* Non-voice device notifications may be unscoped. Input and voice lifecycle
 * messages retain the owner captured by their producer. */
static inline bool vibe_outbound_current(const vibe_outbound_t *message,uint32_t owner,bool releasing) {
    return !message->owner || (message->owner==owner && !releasing);
}
#ifdef __cplusplus
extern "C" {
#endif
bool vibe_message_post_json(QueueHandle_t queue, const char *json, size_t length);
bool vibe_message_post_link_json(QueueHandle_t queue,const char *json,size_t length,uint32_t generation);
bool vibe_outbound_post(QueueHandle_t queue,const char *json,uint32_t owner,vibe_delivery_t delivery);
void vibe_message_release(vibe_msg_t *message);
#ifdef __cplusplus
}
#endif
