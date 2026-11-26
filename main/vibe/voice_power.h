#pragma once
#include <stdbool.h>

/* The network worker calls this only on uplink lifetime transitions. The
 * inactive handle holds no frequency or sleep assertion. */
void vibe_voice_power(bool active);
