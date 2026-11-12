#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stddef.h>
#include "hosts.h"

#define VIBE_JSON_MAX 1536
typedef enum { V_MSG_KEY, V_MSG_JSON, V_MSG_PROVISION, V_MSG_AUDIO_FAIL, V_MSG_LINK_LOST } vibe_msg_type_t;
typedef struct {
    vibe_msg_type_t type;
    int key, event;
    vibe_route_t source;
    /* Only JSON messages own a payload; key events do not reserve 1.5 KiB. */
    char *json;
} vibe_msg_t;
#ifdef __cplusplus
extern "C" {
#endif
bool vibe_message_post_json(QueueHandle_t queue, const char *json, size_t length);
bool vibe_message_post_from(QueueHandle_t queue,const char *json,size_t length,vibe_route_t source);
void vibe_message_release(vibe_msg_t *message);
#ifdef __cplusplus
}
#endif
