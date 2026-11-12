#pragma once
#include "diagnostic_ring.h"
typedef struct cJSON cJSON;
void vibe_diagnostics_record(unsigned code,int32_t detail);
uint32_t vibe_diagnostics_sequence(void);
void vibe_diagnostics_flush(void);
cJSON *vibe_diagnostics_reply(const char *id,uint32_t after);
