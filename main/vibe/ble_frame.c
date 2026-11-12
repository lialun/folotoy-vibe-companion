#include "ble_frame.h"
#include <stdlib.h>
#include <string.h>
static uint16_t get(const uint8_t *b) { return (uint16_t)(b[0]|b[1]<<8); }
void vibe_ble_frame_clear(vibe_ble_frame_t *f) { free(f->bytes);*f=(vibe_ble_frame_t){0}; }
void vibe_ble_frame_header(uint8_t *b,uint16_t id,uint16_t off,uint16_t size) {
    b[0]=id;b[1]=id>>8;b[2]=off;b[3]=off>>8;b[4]=size;b[5]=size>>8;
}
int vibe_ble_frame_add(vibe_ble_frame_t *f,const uint8_t *b,size_t n,uint64_t now) {
    if (n<=VIBE_BLE_HEADER) { vibe_ble_frame_clear(f);return -1; }
    uint16_t id=get(b),off=get(b+2),size=get(b+4);n-=VIBE_BLE_HEADER;
    if (!size || size>VIBE_BLE_PACKET_MAX || off>size || n>size-off) { vibe_ble_frame_clear(f);return -1; }
    if (!off) {
        vibe_ble_frame_clear(f);f->bytes=malloc(size);
        if (!f->bytes) return -1;
        f->id=id;f->size=size;f->at=now;
    }
    if (!f->bytes || f->id!=id || f->size!=size || f->used!=off || now<f->at || now-f->at>2000) {
        vibe_ble_frame_clear(f);return -1;
    }
    memcpy(f->bytes+off,b+VIBE_BLE_HEADER,n);f->used+=(uint16_t)n;
    return f->used==size?1:0;
}
