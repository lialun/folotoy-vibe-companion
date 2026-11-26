#pragma once
#include "messages.h"
#include "voice_feedback.h"
#include <stdlib.h>
#include <string.h>
/* NimBLE callbacks only copy a bounded packet. Parsing and replies run on the
 * network task, avoiding recursive JSON/printf/notify use of the host stack. */
typedef struct { uint16_t handle,length;uint32_t generation;uint8_t bytes[]; } vibe_ble_ingress_t;
static inline bool vibe_ble_ingress_post(QueueHandle_t queue,uint16_t handle,uint32_t generation,const void *bytes,size_t n) {
    if (!queue || !bytes || n<2 || n>VIBE_JSON_MAX+1 || handle==UINT16_MAX) return false;
    const uint8_t *packet=bytes;
    bool feedback=packet[0]==V_VOICE_FEEDBACK_TYPE && n==V_VOICE_FEEDBACK_BYTES;
    if (packet[0]!=2 && packet[0]!=3 && packet[0]!=5 && !feedback) return false;
    vibe_ble_ingress_t *rx=malloc(sizeof(*rx)+n);if (!rx) return false;
    rx->handle=handle;rx->generation=generation;rx->length=(uint16_t)n;memcpy(rx->bytes,packet,n);
    if (xQueueSend(queue,&rx,0)!=pdTRUE) { free(rx);return false; }
    return true;
}

static inline bool vibe_ble_ingress_disconnect(QueueHandle_t queue,uint16_t handle) {
    if (!queue || handle==UINT16_MAX) return false;
    vibe_ble_ingress_t *rx=malloc(sizeof(*rx));if (!rx) return false;
    memset(rx,0,sizeof(*rx));
    rx->handle=handle;
    if (xQueueSend(queue,&rx,0)!=pdTRUE) { free(rx);return false; }
    return true;
}
