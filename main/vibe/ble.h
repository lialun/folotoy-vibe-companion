#pragma once
#include "esp_err.h"
#include "voice_diagnostics.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
esp_err_t vibe_ble_init(void);
/* Audio worker intent only; GAP procedures remain serialized on the host. */
void vibe_ble_streaming(bool active);
/* Worker-only; bounded lock/pool wait shares one 30 ms voice / 50 ms control budget. */
int vibe_ble_send_to(uint16_t handle,unsigned char type,const void *bytes,size_t length);
/* Network-worker voice: never wait for TX lock or mbuf availability.
 * Zero: complete local submission; positive: resumable prefix advanced; negative: no
 * bytes submitted in this attempt. Only the sender owns the byte cursor;
 * the scheduler owns order and preserves an existing lease across no credit. */
int vibe_ble_voice_submit(uint16_t handle,unsigned char type,const void *bytes,size_t length,
                           uint64_t session,uint32_t sequence);
void vibe_ble_voice_report(void);
vibe_ble_voice_pressure_t vibe_ble_voice_pressure(void);
void vibe_ble_voice_link(vibe_voice_counters_t *counters);
void vibe_ble_publish_metadata(const char *text,size_t length);
/* Stop accepting/downloading radio assemblies before handing RAM to voice. */
void vibe_ble_radio_ingress(bool enabled);
/* These entry points share the LAN control-owner policy. */
bool vibe_ble_generation_matches(uint16_t handle,uint32_t generation);
uint32_t vibe_ble_generation(uint16_t handle);
void vibe_receive_ble(uint16_t handle,uint32_t generation,const void *packet,size_t length);
void vibe_ble_disconnected(uint16_t handle);
int vibe_discovery_json(char *buffer,size_t capacity,bool authorized,bool auth_checked);
