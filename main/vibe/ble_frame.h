#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Six-byte LE header: message number, offset, whole packet length (incl type). */
#define VIBE_BLE_HEADER 6
#define VIBE_BLE_PACKET_MAX 1537
typedef struct { uint8_t *bytes;uint16_t id,size,used;uint64_t at; } vibe_ble_frame_t;
void vibe_ble_frame_clear(vibe_ble_frame_t *f);
/* 1 complete, 0 partial, -1 rejected. Complete buffer remains owned by f. */
int vibe_ble_frame_add(vibe_ble_frame_t *f,const uint8_t *bytes,size_t size,uint64_t now);
void vibe_ble_frame_header(uint8_t *bytes,uint16_t id,uint16_t offset,uint16_t size);
