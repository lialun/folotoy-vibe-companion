#include "vibe.h"
#include "voice_uplink.h"
#include "ble.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <errno.h>
#include <stdlib.h>

static SemaphoreHandle_t mutex;
static vibe_voice_uplink_ports_t ports;
static vibe_voice_tx_t tx;
static unsigned storage_capacity;
static vibe_voice_counters_t totals;
static uint64_t next_service;
static bool activity;
static vibe_voice_mode_t wire_mode=V_VOICE_STREAM;
/* The sole network worker owns this in-flight copy. */
static uint8_t body[VIBE_VOICE_BODY];

esp_err_t vibe_voice_uplink_init(vibe_voice_uplink_ports_t value) {
    if (!value.owner || !value.links || !value.send || !value.wake || !value.acquire || !value.release) return ESP_ERR_INVALID_ARG;
    if (!mutex) mutex=xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    ports=value;
    return ESP_OK;
}

unsigned vibe_voice_begin(uint64_t session,uint32_t owner,vibe_voice_mode_t mode,bool feedback) {
    if (!mutex || !session || !owner || mode<V_VOICE_LEGACY || mode>V_VOICE_STREAM || owner!=ports.owner()) return 0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (!tx.slots) {
        tx.slots=ports.acquire(&storage_capacity);
    }
    unsigned capacity=tx.slots?(feedback?storage_capacity:(storage_capacity<V_VOICE_TX_SLOTS?storage_capacity:V_VOICE_TX_SLOTS)):0;
    if (capacity) {
        vibe_voice_tx_begin(&tx,tx.slots,capacity,session,owner);
        tx.feedback=feedback;tx.retention_ms=feedback?capacity*100:V_VOICE_RETAIN_MS;
        totals.feedback_id=totals.confirmed=totals.retired=0;
        totals.history=capacity;totals.retention=tx.retention_ms;
    }
    wire_mode=mode;
    xSemaphoreGive(mutex);
    if (capacity) vibe_ble_probe_recording(true);
    return capacity;
}

int vibe_send_voice(const void *bytes, size_t length, uint32_t owner) {
    if (!mutex) return -1;
    uint64_t now=vibe_now();
    xSemaphoreTake(mutex,portMAX_DELAY);
    vibe_voice_mode_t mode=wire_mode;
    xSemaphoreGive(mutex);
    unsigned links=ports.links(owner,mode);
    xSemaphoreTake(mutex,portMAX_DELAY);
    unsigned overwritten=tx.overwritten;
    bool queued=vibe_voice_tx_enqueue(&tx,bytes,length,owner,links,now);
    totals.replaced+=tx.overwritten-overwritten;
    xSemaphoreGive(mutex);
    if (queued) ports.wake();
    return queued ? 0 : -1;
}

void vibe_voice_end(uint64_t session, uint32_t owner) {
    if (!mutex) return;
    vibe_ble_probe_recording(false);
    xSemaphoreTake(mutex,portMAX_DELAY);
    vibe_voice_tx_end(&tx,session,owner,vibe_now());
    xSemaphoreGive(mutex);
    ports.wake();
}

bool vibe_voice_repair_request(uint64_t session, const uint32_t *frames,
                               unsigned count, uint32_t owner,uint32_t request_id) {
    if (!mutex || !frames || !count || count>VIBE_VOICE_MAX_REPAIR || owner!=ports.owner()) return false;
    unsigned links=ports.links(owner,V_VOICE_STREAM);
    bool accepted=false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (tx.owner==owner) for (unsigned i=0;i<count;i++)
        accepted=vibe_voice_tx_repair(&tx,session,frames[i],links,vibe_now(),request_id) || accepted;
    xSemaphoreGive(mutex);
    if (accepted) ports.wake();
    return accepted;
}

void vibe_voice_uplink_ready(uint64_t session, uint32_t owner) {
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (tx.recording && !tx.cancelled && tx.session==session && tx.owner==owner) tx.ready=true;
    xSemaphoreGive(mutex);
}

bool vibe_voice_uplink_is_ready(uint64_t session, uint32_t owner) {
    if (!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ready=tx.recording && !tx.cancelled && tx.session==session && tx.owner==owner && tx.ready;
    xSemaphoreGive(mutex);
    return ready;
}

void vibe_voice_uplink_cancel(uint64_t session, uint32_t owner) {
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (tx.session==session && tx.owner==owner) {
        tx.cancelled=true;
        tx.leased=0;
        for (unsigned i=0;i<tx.capacity;i++) tx.slots[i].pending=0;
    }
    xSemaphoreGive(mutex);
}

bool vibe_voice_uplink_cancelled(uint64_t session, uint32_t owner) {
    if (!mutex) return true;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool cancelled=tx.session!=session || tx.owner!=owner || tx.cancelled;
    xSemaphoreGive(mutex);
    return cancelled;
}

static void count_network_error(int error) {
    totals.wifi_error=error;
    if (error==ENOMEM || error==ENOBUFS) ++totals.wifi_oom;
    else if (error==EAGAIN || error==EWOULDBLOCK) ++totals.wifi_busy;
    else ++totals.wifi_other;
}

bool vibe_voice_uplink_can_overtake_control(void) {
    if (!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool acknowledged=tx.slots && tx.ready && !tx.cancelled;
    xSemaphoreGive(mutex);
    return acknowledged;
}

void vibe_voice_uplink_service(bool may_transmit) {
    if (!mutex) return;
    uint64_t now=vibe_now();
    if (now<next_service) return;
    next_service=now+V_VOICE_RETRY_MS;
    uint32_t owner=ports.owner();
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (tx.slots && !vibe_voice_tx_live(&tx,owner,now)) {
        ports.release(tx.slots);
        tx=(vibe_voice_tx_t){0};
    }
    bool active=tx.slots && !tx.cancelled;
    xSemaphoreGive(mutex);
    if (active!=activity) {
        activity=active;
        if (ports.activity) ports.activity(active);
    }
    if (!may_transmit) return;

    /* At most one send per leg per turn. Failed notifications back off instead
     * of consuming every loop while lwIP/control/credit events need service. */
    for (unsigned leg=0;leg<2;leg++) {
        vibe_voice_tx_ticket_t ticket;
        xSemaphoreTake(mutex,portMAX_DELAY);
        vibe_voice_mode_t mode=wire_mode;
        xSemaphoreGive(mutex);
        if (!(ports.links(owner,mode)&(1u<<leg))) continue;
        xSemaphoreTake(mutex,portMAX_DELAY);
        unsigned expired=tx.expired;
        /* Capture can advance during another carrier's stack call. Sample
         * under the ring lock so fresh sibling copies cannot appear future-dated. */
        bool ready=wire_mode==mode && vibe_voice_tx_take(&tx,leg,vibe_now(),body,&ticket);
        totals.expired+=tx.expired-expired;
        xSemaphoreGive(mutex);
        if (!ready) continue;
        int network_error=0;
        vibe_voice_submission_t result=ports.send(mode,&ticket,body,sizeof(body),&network_error);
        xSemaphoreTake(mutex,portMAX_DELAY);
        expired=tx.expired;
        vibe_voice_tx_complete(&tx,&ticket,vibe_now(),result);
        totals.expired+=tx.expired-expired;
        if (!result) {
            if (leg==0) ++totals.wifi; else ++totals.ble;
            if (ticket.repair) ++totals.repairs;
        } else if (result==V_VOICE_NOT_SUBMITTED) {
            ++totals.failed;
            if (leg==0 && network_error) count_network_error(network_error);
        }
        xSemaphoreGive(mutex);
    }
}

unsigned vibe_voice_uplink_wait(unsigned maximum_ms) {
    if (!mutex) return maximum_ms;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool live=tx.slots!=NULL;
    xSemaphoreGive(mutex);
    return live && maximum_ms>V_VOICE_RETRY_MS ? V_VOICE_RETRY_MS : maximum_ms;
}

vibe_voice_counters_t vibe_voice_counters(void) {
    vibe_voice_counters_t value={0};
    if (mutex) {
        xSemaphoreTake(mutex,portMAX_DELAY);
        value=totals;
        xSemaphoreGive(mutex);
    }
    value.pressure=vibe_ble_voice_pressure();
    value.radio=vibe_ble_radio_probe();
    vibe_ble_voice_link(&value);
    return value;
}

void vibe_voice_report(void) {
    vibe_voice_counters_t value=vibe_voice_counters();
    ESP_LOGI("vibe_voice","Delivery wifi=%u ble=%u failed=%u overwritten=%u expired=%u repaired=%u",
             value.wifi,value.ble,value.failed,value.replaced,value.expired,value.repairs);
    vibe_ble_voice_report();
}

bool vibe_voice_uplink_feedback(const vibe_voice_feedback_t *feedback,uint32_t owner) {
    if (!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool accepted=tx.slots && vibe_voice_tx_feedback(&tx,feedback,owner);
    if (accepted) {totals.feedback_id=tx.feedback_id;totals.confirmed=tx.confirmed;totals.retired=tx.retired;}
    xSemaphoreGive(mutex);
    if (accepted) ports.wake();
    return accepted;
}
