#pragma once
#include <stdint.h>

/* Audio-worker only. The request ID makes cleanup conditional on the capture
 * that actually finished; a queued retry cannot be cancelled by its predecessor. */
void vibe_voice_capture(int16_t *pcm);
