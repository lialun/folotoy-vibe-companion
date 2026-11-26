#include "messages.h"
#include <stdlib.h>
#include <string.h>

bool vibe_message_post_json(QueueHandle_t queue,const char *json,size_t length) {
    return vibe_message_post_link_json(queue,json,length,0);
}
bool vibe_message_post_link_json(QueueHandle_t queue, const char *json, size_t length,uint32_t generation) {
    if (!queue || !json || !length || length>VIBE_JSON_MAX || memchr(json,0,length)) return false;
    vibe_msg_t message={.type=V_MSG_JSON,.link_generation=generation,.json=malloc(length+1)};
    if (!message.json) return false;
    memcpy(message.json,json,length);message.json[length]=0;
    if (xQueueSend(queue,&message,0)==pdTRUE) return true;
    vibe_message_release(&message);return false;
}
void vibe_message_release(vibe_msg_t *message) {
    if (message && message->type==V_MSG_JSON) { free(message->json);message->json=NULL; }
}

bool vibe_outbound_post(QueueHandle_t queue,const char *json,uint32_t owner,vibe_delivery_t delivery) {
    if (!queue || !json || (unsigned)delivery>V_DELIVERY_CRITICAL) return false;
    size_t length=strlen(json);
    if (!length || length>VIBE_JSON_MAX) return false;
    vibe_outbound_t message={.owner=owner,.delivery=delivery,.json=malloc(length+1)};
    if (!message.json) return false;
    memcpy(message.json,json,length+1);
    if (xQueueSend(queue,&message,0)==pdTRUE) return true;
    free(message.json);
    return false;
}
