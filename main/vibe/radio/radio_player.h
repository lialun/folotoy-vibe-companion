#pragma once
#include <cstdint>
// Catalog record retained from passport-os (MIT). Playback is owned by Vibe's audio worker.
struct RadioStation {
    char name[64], description[96], url[257];
    uint16_t frequency_decihz;
};
