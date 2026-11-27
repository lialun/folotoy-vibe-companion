#pragma once
#include "connection_request.h"
#include <stddef.h>

#define VIBE_TRUSTED_MAX 4
#define VIBE_TRUSTED_BLOB_MAX (5 + VIBE_TRUSTED_MAX * 100)
typedef struct { char client[37],token[65]; } vibe_trusted_computer_t;
typedef struct { uint8_t count;vibe_trusted_computer_t entries[VIBE_TRUSTED_MAX]; } vibe_trusted_computers_t;

bool vibe_trusted_matches(const vibe_trusted_computers_t *s,const char *client,const char *token);
bool vibe_trusted_has(const vibe_trusted_computers_t *s,const char *client);
bool vibe_trusted_put(vibe_trusted_computers_t *s,const char *client,const char *token);
bool vibe_trusted_remove(vibe_trusted_computers_t *s,const char *client);
size_t vibe_trusted_encode(const vibe_trusted_computers_t *s,void *bytes,size_t capacity);
bool vibe_trusted_decode(vibe_trusted_computers_t *s,const void *bytes,size_t length);
