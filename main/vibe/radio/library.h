#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VIBE_STATION_MAX 20
typedef struct { char name[49],url[257]; } vibe_station_t;
typedef struct { uint32_t version,revision,count; vibe_station_t stations[]; } vibe_station_list_t;
typedef struct {
    vibe_station_list_t *active,*staged;
    uint64_t received;
    uint64_t expires;
    char transaction[37],committed[37];
} vibe_library_t;
typedef bool (*vibe_library_save_t)(const vibe_station_list_t *,void *);
bool vibe_station_valid(const vibe_station_t *station);
bool vibe_station_list_valid(const vibe_station_list_t *list);
size_t vibe_station_list_bytes(unsigned count);
vibe_station_list_t *vibe_station_list_create(unsigned count);
/* Compact string storage; RAM remains a count-sized array for indexed access. */
size_t vibe_station_list_storage_bytes(const vibe_station_list_t *list);
bool vibe_station_list_encode(const vibe_station_list_t *list,void *bytes,size_t size);
/* Accept compact storage, exact-size v1 lists and the previous fixed 16-entry blob. */
vibe_station_list_t *vibe_station_list_decode(const void *bytes,size_t size);
void vibe_library_clear(vibe_library_t *library);
void vibe_library_expire(vibe_library_t *library,uint64_t now);
void vibe_library_abort(vibe_library_t *library);
/* Caller authenticates and serializes access. One bounded station per UDP reply. */
cJSON *vibe_library_handle(vibe_library_t *library,const cJSON *request,uint64_t now,vibe_library_save_t save,void *context);
#ifdef __cplusplus
}
#endif
