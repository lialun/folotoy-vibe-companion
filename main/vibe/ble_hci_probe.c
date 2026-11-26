#include "ble_hci_probe.h"
#include "voice_diagnostics.h"
#include "os/os_mbuf.h"
#include "esp_timer.h"

/* Observe the existing transport implementation without changing its callback,
 * buffer ownership, return value or flow control. Bound to ESP-IDF 5.5.3's
 * transport ABI; the firmware link map verifies the observation points. */
static vibe_hci_probe_t probe;
extern int __real_ble_transport_to_ll_acl_impl(struct os_mbuf *buffer);
extern int __real_ble_transport_to_hs_evt_impl(void *buffer);
struct ble_hci_ev;
extern int __real_ble_hs_hci_evt_process(struct ble_hci_ev *event);
int __wrap_ble_transport_to_ll_acl_impl(struct os_mbuf *buffer) {
    unsigned bytes=OS_MBUF_PKTLEN(buffer);
    int result=__real_ble_transport_to_ll_acl_impl(buffer);
    vibe_hci_probe_tx(&probe,bytes,result);return result;
}
int __wrap_ble_transport_to_hs_evt_impl(void *buffer) {
    const uint8_t *bytes=buffer;
    if(bytes)vibe_hci_probe_event(&probe,bytes,(size_t)bytes[1]+2u,(unsigned)(esp_timer_get_time()/1000));
    return __real_ble_transport_to_hs_evt_impl(buffer);
}
int __wrap_ble_hs_hci_evt_process(struct ble_hci_ev *event) {
    const uint8_t *bytes=(const uint8_t *)event;
    unsigned began=(unsigned)(esp_timer_get_time()/1000);bool active=atomic_load(&probe.recording);
    if(bytes)vibe_hci_probe_processed(&probe,bytes,(size_t)bytes[1]+2u,began);
    int result=__real_ble_hs_hci_evt_process(event);
    if(active)vibe_hci_probe_duration(&probe,(unsigned)(esp_timer_get_time()/1000)-began);
    return result;
}
void vibe_ble_probe_recording(bool active) {vibe_hci_probe_record(&probe,active,(unsigned)(esp_timer_get_time()/1000));}
vibe_ble_radio_probe_t vibe_ble_radio_probe(void) {
    return (vibe_ble_radio_probe_t){
        .packets=atomic_load(&probe.packets),.bytes=atomic_load(&probe.bytes),
        .completed=atomic_load(&probe.completed),.failures=atomic_load(&probe.failures),
        .gap_ms=atomic_load(&probe.max_gap),.processed=atomic_load(&probe.processed),
        .host_lag=atomic_load(&probe.max_host_lag),.process_ms=atomic_load(&probe.max_process),.untracked=atomic_load(&probe.untracked)};
}
