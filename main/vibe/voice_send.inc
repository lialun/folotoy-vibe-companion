/* Included by network.c. A slow backup never owns packet_mutex or blocks the
 * capture task's UDP send. The network worker alone drains copy_active. */
static SemaphoreHandle_t voice_copy_mutex;
static vibe_voice_copy_t voice_pending,voice_active;
static vibe_voice_history_t voice_history;
static struct {unsigned wifi,ble,failed,replaced,expired,retry_queued,wifi_retries,repair_requests,repair_sent,repair_miss;} voice_tx;
static bool voice_owner_current(uint32_t owner) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=owner==owner_generation && !release_after_send;
    xSemaphoreGive(peer_mutex);return current;
}
unsigned vibe_voice_links(uint32_t owner) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=owner==owner_generation && !release_after_send;
    bool wifi=current && vibe_peer_use_wifi(vibe_now(),wifi_seen,0,peer.sin_addr.s_addr && peer.sin_port,false,false);
    unsigned links=(wifi?1u:0u)|(current && ble_owner?2u:0u);
    xSemaphoreGive(peer_mutex);return links;
}
unsigned vibe_voice_begin(uint64_t session,uint32_t owner,bool repair) {
    if(!voice_copy_mutex || !session || !voice_owner_current(owner))return 0;
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    if(!repair && voice_history.slots){free(voice_history.slots);voice_history=(vibe_voice_history_t){0};}
    if(repair && voice_history.slots) {
        unsigned count=voice_history.count;vibe_voice_history_slot_t *slots=voice_history.slots;
        vibe_voice_history_init(&voice_history,slots,count,session,owner);
        xSemaphoreGive(voice_copy_mutex);return count;
    }
    xSemaphoreGive(voice_copy_mutex);
    if(!repair)return 0;
    /* One allocation per recording; preserve a 12 KiB control/codec reserve.
     * Failure degrades to the already working unbuffered path. Idle owns none. */
    unsigned count=VIBE_VOICE_HISTORY_MAX;vibe_voice_history_slot_t *slots=NULL;
    for(;count>=2;count-=2) {
        size_t bytes=count*sizeof(*slots);
        if(heap_caps_get_free_size(MALLOC_CAP_8BIT)>=bytes+12288 &&
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)>=bytes)slots=malloc(bytes);
        if(slots)break;
    }
    if(!slots)return 0;
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    vibe_voice_history_init(&voice_history,slots,count,session,owner);
    xSemaphoreGive(voice_copy_mutex);return count;
}
void vibe_voice_end(uint64_t session,uint32_t owner) {
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    if(voice_history.session==session && voice_history.owner==owner) {
        voice_history.recording=false;voice_history.release_at=vibe_now()+450;
    }
    xSemaphoreGive(voice_copy_mutex);vibe_network_wake();
}
static bool voice_repair_request(uint64_t session,const uint32_t *frames,unsigned count,uint32_t owner) {
    if(!session || !frames || !count || count>4 || !voice_owner_current(owner))return false;
    uint64_t now=vibe_now();bool queued=false;
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    if(voice_history.owner==owner)for(unsigned i=0;i<count;i++) {
        if(vibe_voice_history_request(&voice_history,session,frames[i],now)){++voice_tx.repair_requests;queued=true;}
        else ++voice_tx.repair_miss;
    }
    xSemaphoreGive(voice_copy_mutex);return queued;
}
static bool voice_copy_current(const vibe_voice_copy_t *copy) {
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=copy->owner==owner_generation && !release_after_send;
    xSemaphoreGive(peer_mutex);
    return current && vibe_ble_generation_matches(copy->handle,copy->link);
}
static int voice_ble_send(const vibe_voice_copy_t *copy) {
    if (!voice_copy_current(copy)) return -1;
    return vibe_ble_send_to(copy->handle,VIBE_VOICE_TYPE,copy->bytes,sizeof(copy->bytes));
}
static void voice_history_flush(void) {
    uint32_t owner=vibe_network_generation();uint64_t now=vibe_now();vibe_voice_attempt_t attempt;
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    if(voice_history.slots && (voice_history.owner!=owner ||
       (!voice_history.recording && now>=voice_history.release_at))) {
        free(voice_history.slots);voice_history=(vibe_voice_history_t){0};
    }
    bool send=vibe_voice_history_take(&voice_history,now,voice_active.bytes,&attempt);
    xSemaphoreGive(voice_copy_mutex);
    if(!send)return;
    struct sockaddr_in wifi,ble;bool has_wifi,has_ble;
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool current=attempt.owner==owner_generation && !release_after_send;
    wifi=peer;ble=ble_peer;has_ble=current && ble_owner;
    has_wifi=current && vibe_peer_use_wifi(now,wifi_seen,0,wifi.sin_addr.s_addr && wifi.sin_port,false,false);
    xSemaphoreGive(peer_mutex);
    bool sent=false;
    if(has_wifi) {
        uint8_t packet[VIBE_VOICE_BODY+1];packet[0]=VIBE_VOICE_TYPE;memcpy(packet+1,voice_active.bytes,VIBE_VOICE_BODY);
        sent=sendto(udp,packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr *)&wifi,sizeof(wifi))==(int)sizeof(packet);
    }
    if(has_ble && attempt.kind==V_VOICE_REPAIR) {
        voice_active.owner=attempt.owner;voice_active.handle=ntohs(ble.sin_port)-1;
        voice_active.link=vibe_ble_generation(voice_active.handle);
        if(!voice_ble_send(&voice_active))sent=true;
    }
    now=vibe_now();
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    vibe_voice_history_complete(&voice_history,&attempt,now,sent);
    if(attempt.kind==V_VOICE_REPAIR){if(sent)++voice_tx.repair_sent;}
    else ++voice_tx.wifi_retries;
    xSemaphoreGive(voice_copy_mutex);
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
        owner=vibe_network_generation();now=vibe_now();
        bool retry=result && voice_copy_current(&voice_active);
        xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
        if (!result) ++voice_tx.ble;else ++voice_tx.failed;
        if (retry && vibe_voice_copy_retry(&voice_pending,&voice_active,now,owner)) ++voice_tx.retry_queued;
        xSemaphoreGive(voice_copy_mutex);
    }
    voice_history_flush();
}
static unsigned voice_copy_wait(unsigned wait_ms) {
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    bool pending=voice_pending.valid || vibe_voice_history_pending(&voice_history);
    xSemaphoreGive(voice_copy_mutex);
    return pending && wait_ms>20 ? 20:wait_ms;
}
/* USB console writes can wait for host drainage. Keep diagnostic I/O out of
 * the live copy worker; take this snapshot only after capture has ended. */
void vibe_voice_report(void) {
    if (!voice_copy_mutex) return;
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    unsigned wifi=voice_tx.wifi,ble=voice_tx.ble,failed=voice_tx.failed,replaced=voice_tx.replaced,expired=voice_tx.expired,retries=voice_tx.retry_queued;
    xSemaphoreGive(voice_copy_mutex);
    ESP_LOGI(TAG,"Voice copies cumulative wifi=%u ble=%u failed=%u replaced=%u expired=%u retry_queued=%u",wifi,ble,failed,replaced,expired,retries);
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    unsigned local=voice_tx.wifi_retries,requested=voice_tx.repair_requests,sent=voice_tx.repair_sent,miss=voice_tx.repair_miss;
    xSemaphoreGive(voice_copy_mutex);
    ESP_LOGI(TAG,"Voice repair cumulative wifi_retries=%u requested=%u sent=%u cache_miss=%u",local,requested,sent,miss);
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
    xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
    (void)vibe_voice_history_store(&voice_history,bytes,expected_owner,now);
    xSemaphoreGive(voice_copy_mutex);
    /* A stack-local datagram cannot race send_packet's shared buffer. */
    int result=-1,wifi_error=0;
    if (has_wifi) {
        uint8_t packet[VIBE_VOICE_BODY+1];packet[0]=VIBE_VOICE_TYPE;memcpy(packet+1,bytes,n);
        result=sendto(udp,packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr *)&wifi,sizeof(wifi))==(int)sizeof(packet)?0:-1;
        if(result)wifi_error=errno;
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
                bool queued=result && voice_copy_current(&copy);
                xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
                if (!result) ++voice_tx.ble;else ++voice_tx.failed;
                if (queued) {
                    /* Admission to the existing slot is not delivery. Retry on
                     * the worker after transient lock/pool pressure, retaining
                     * the original 200 ms lifetime and latest-frame bound. */
                    if (vibe_voice_copy_offer(&voice_pending,bytes,n,now,expected_owner,handle,link)) ++voice_tx.replaced;
                    ++voice_tx.retry_queued;result=0;
                }
                xSemaphoreGive(voice_copy_mutex);
                if (queued) vibe_network_wake();
            }
        }
    }
    if(result && has_wifi && (wifi_error==ENOMEM || wifi_error==ENOBUFS || wifi_error==EAGAIN || wifi_error==EWOULDBLOCK)) {
        xSemaphoreTake(voice_copy_mutex,portMAX_DELAY);
        bool queued=vibe_voice_history_retry_wifi(&voice_history,vibe_voice_session(bytes),vibe_voice_sequence(bytes),vibe_now());
        xSemaphoreGive(voice_copy_mutex);
        if(queued){result=0;vibe_network_wake();}
    }
    if (!result) {
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        if (expected_owner==owner_generation) last_sent_ms=vibe_now();
        xSemaphoreGive(peer_mutex);
    }
    return result;
}
