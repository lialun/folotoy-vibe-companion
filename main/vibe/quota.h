#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct cJSON;
#define V_QUOTA_SLOTS 7
#define V_QUOTA_METRICS 4
#define V_QUOTA_TTL_MS 930000U
typedef struct {
    char label[25],amount[17],currency[5];
    uint64_t reset_ms;
    uint32_t minutes;
    uint16_t remaining; /* Basis points, never inferred from token usage. */
    uint8_t kind; /* 1 = remaining percent; 2 = independent currency balance. */
} vibe_quota_metric_t;
typedef struct {
    char agent[17],name[33];
    uint64_t sample,owner,stamp,expires;
    unsigned count;
    vibe_quota_metric_t metrics[V_QUOTA_METRICS];
} vibe_quota_card_t;
typedef struct { vibe_quota_card_t cards[V_QUOTA_SLOTS]; } vibe_quota_store_t;
bool vibe_quota_accept(vibe_quota_store_t *store,const struct cJSON *object,uint64_t now);
bool vibe_quota_clear(vibe_quota_store_t *store,const struct cJSON *object);
unsigned vibe_quota_pages(const vibe_quota_store_t *store,uint64_t now);
/* Put all live quota pages first, followed by the weather pages. */
bool vibe_quota_select(unsigned selection,unsigned weather_pages,unsigned quota_pages,unsigned *index);
/* Returns up to three live metrics (three percentage windows share one page). Missing/reset/expired rows never become 0%. */
bool vibe_quota_page(const vibe_quota_store_t *store,uint64_t now,unsigned page,
                     vibe_quota_card_t *card,unsigned *part,unsigned *parts);
const char *vibe_quota_label(const vibe_quota_metric_t *metric,char *buffer,unsigned capacity);
void vibe_quota_reset_text(uint64_t deadline,uint64_t now,char *buffer,unsigned capacity);
const char *vibe_quota_amount(const char *amount,char *buffer,unsigned capacity);
#ifdef __cplusplus
}
#endif
