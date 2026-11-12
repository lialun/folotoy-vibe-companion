#include "messages.h"
#include <stdlib.h>
#include <string.h>

bool vibe_message_post_json(QueueHandle_t queue, const char *json, size_t length) {
    return vibe_message_post_from(queue,json,length,(vibe_route_t){0});
}
bool vibe_message_post_from(QueueHandle_t queue,const char *json,size_t length,vibe_route_t source) {
    if (!queue || !json || !length || length>VIBE_JSON_MAX || memchr(json,0,length)) return false;
    vibe_msg_t message={.type=V_MSG_JSON,.json=malloc(length+1),.source=source};
    if (!message.json) return false;
    memcpy(message.json,json,length);message.json[length]=0;
    if (xQueueSend(queue,&message,0)==pdTRUE) return true;
    vibe_message_release(&message);return false;
}
void vibe_message_release(vibe_msg_t *message) {
    if (message && message->type==V_MSG_JSON) { free(message->json);message->json=NULL; }
}
