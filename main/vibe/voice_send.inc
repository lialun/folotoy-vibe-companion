/* Included by network.c. A slow backup never owns packet_mutex or blocks the
 * capture task's UDP send. The network worker alone drains copy_active. */
static SemaphoreHandle_t voice_copy_mutex;
static vibe_voice_copy_t voice_pending,voice_active;
static struct {unsigned wifi,ble,failed,replaced,expired;} voice_tx;
static int voice_ble_send(const vibe_voice_copy_t *copy) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=copy->owner==owner_generation && !release_after_send;
    xSemaphoreGive(peer_mutex);
    if (!current || !vibe_ble_generation_matches(copy->handle,copy->link)) return -1;
    return vibe_ble_send_to(copy->handle,VIBE_VOICE_TYPE,copy->bytes,sizeof(copy->bytes));
}
static void voice_copy_flush(void) {
    uint32_t owner=vibe_network_generation();uint64_t now=vibe_now();
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    bool pending=voice_pending.valid;
    bool send=vibe_voice_copy_take(&voice_pending,&voice_active,now,owner);
    if (pending && !send) ++voice_tx.expired;
    xSemaphoreGive(voice_copy_mutex);
    if (send) {
        int result=voice_ble_send(&voice_active);
        xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
        if (!result) ++voice_tx.ble;else ++voice_tx.failed;
        xSemaphoreGive(voice_copy_mutex);
    }
}
/* USB console writes can wait for host drainage. Keep diagnostic I/O out of
 * the live copy worker; take this snapshot only after capture has ended. */
void vibe_voice_report(void) {
    if (!voice_copy_mutex) return;
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    unsigned wifi=voice_tx.wifi,ble=voice_tx.ble,failed=voice_tx.failed,replaced=voice_tx.replaced,expired=voice_tx.expired;
    xSemaphoreGive(voice_copy_mutex);
    ESP_LOGI(TAG,"Voice copies cumulative wifi=%u ble=%u failed=%u replaced=%u expired=%u",wifi,ble,failed,replaced,expired);
}
int vibe_send_voice(const void *bytes,size_t n,uint32_t expected_owner) {
    if (udp<0 || !voice_copy_mutex || !bytes || n!=VIBE_VOICE_BODY || !expected_owner) return -1;
    struct sockaddr_in wifi,ble;bool has_wifi,has_ble;
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    uint64_t now=vibe_now();bool current=owner_generation==expected_owner && !release_after_send;
    wifi=peer;ble=ble_peer;has_ble=current && ble_owner;
    has_wifi=current && vibe_peer_use_wifi(now,wifi_seen,0,wifi.sin_addr.s_addr && wifi.sin_port,false,false);
    xSemaphoreGive(peer_mutex);
    if (!current) return -1;
    /* A stack-local datagram cannot race send_packet's shared buffer. */
    int result=-1;
    if (has_wifi) {
        uint8_t packet[VIBE_VOICE_BODY+1];packet[0]=VIBE_VOICE_TYPE;memcpy(packet+1,bytes,n);
        result=sendto(udp,packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr *)&wifi,sizeof(wifi))==(int)sizeof(packet)?0:-1;
        xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
        if (!result) ++voice_tx.wifi;else ++voice_tx.failed;
        xSemaphoreGive(voice_copy_mutex);
    }
    if (has_ble) {
        uint16_t handle=ntohs(ble.sin_port)-1;uint32_t link=vibe_ble_generation(handle);
        if (link) {
            if (!result) {
                xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
                if (vibe_voice_copy_offer(&voice_pending,bytes,n,now,expected_owner,handle,link)) ++voice_tx.replaced;
                xSemaphoreGive(voice_copy_mutex);vibe_network_wake();
            } else {
                /* With no working UDP submission retain the proven direct BLE
                 * path. Never queue seconds of speech behind a failing radio. */
                vibe_voice_copy_t copy={0};vibe_voice_copy_offer(&copy,bytes,n,now,expected_owner,handle,link);
                result=voice_ble_send(&copy);
                xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
                if (!result) ++voice_tx.ble;else ++voice_tx.failed;
                xSemaphoreGive(voice_copy_mutex);
            }
        }
    }
    if (!result) {
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        if (expected_owner==owner_generation) last_sent_ms=vibe_now();
        xSemaphoreGive(peer_mutex);
    }
    return result;
}
