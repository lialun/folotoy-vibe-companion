#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Caller frees the bounded complete response. A cancelled/failed request returns NULL. */
char *vibe_weather_fetch(const char *url,bool (*allowed)(void),uint64_t (*clock_ms)(void));
