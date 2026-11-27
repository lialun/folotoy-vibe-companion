#include "voice_diagnostics.h"
#include "ble_voice_cursor.h"
static vibe_ble_voice_pressure_t voice_pressure;
static vibe_ble_voice_cursor_t voice_cursor;
static uint16_t voice_probe_handle=UINT16_MAX;
static struct { unsigned bytes,header,leading; } att_layout;
static int send_to(uint16_t conn,unsigned char type,const void *bytes,size_t length,bool background,
                   uint64_t session,uint32_t sequence) {
    const bool audio=type==1 || type==6;uint64_t began=vibe_now();
    /* Only workers call this sender. Share the existing whole-packet budget
     * with lock acquisition; an overlapping control packet must not cause an
     * otherwise healthy BLE-only voice frame to be discarded immediately. */
    if (!tx_mutex || xSemaphoreTake(tx_mutex,pdMS_TO_TICKS(background?0:vibe_ble_tx_lock_ms(audio)))!=pdTRUE) return -1;
    if (!ble_enabled || !ready) {xSemaphoreGive(tx_mutex);return -1;}
    xSemaphoreTake(links_mutex,portMAX_DELAY);ble_link_t *link=find_link(conn);
    bool subscribed=link && link->subscribed;
    uint32_t generation=link?link->generation:0;
    xSemaphoreGive(links_mutex);
    bool encrypted=secure(conn);
    if (!subscribed || !encrypted || length+1>VIBE_BLE_PACKET_MAX) {
        if (background) voice_cursor.active=false;
        xSemaphoreGive(tx_mutex);return -1;
    }
    if(audio)voice_probe_handle=conn;
    unsigned mtu=ble_att_mtu(conn),capacity=mtu>10 ? mtu-3-VIBE_BLE_HEADER : 0;
    if (!capacity) { if(background)voice_cursor.active=false;xSemaphoreGive(tx_mutex);return -1; }
    if (capacity>240) capacity=240;
    uint8_t id,frame[247];size_t total=length+1,offset=0;
    if (background) {
        if (vibe_ble_voice_prepare(&voice_cursor,conn,generation,session,sequence,type,total,began))
            voice_cursor.message=++message_id;
        id=voice_cursor.message;offset=voice_cursor.offset;
    } else id=++message_id;
    const size_t initial_offset=offset;
    for (;offset<total;) {
        size_t n=total-offset;if (n>capacity) n=capacity;
        int result=BLE_HS_ENOMEM;unsigned free_total=0,min_pool=0;
        do {
            struct os_mbuf *buffer=ble_hs_mbuf_att_pkt();
            /* The SDK's default ATT allocator always chooses its smallest
             * pool. Preserve its header/headroom, but prefer a wider block for
             * audio when that pool is busy or would require extra fragments.
             * This reuses existing pools; it does not reserve more heap. */
            unsigned small_free=0;
            if (buffer) {
                uint32_t critical;OS_ENTER_CRITICAL(critical);
                small_free=buffer->om_omp->omp_pool->mp_num_free;
                OS_EXIT_CRITICAL(critical);
                att_layout.bytes=buffer->om_omp->omp_databuf_len;
                att_layout.header=buffer->om_pkthdr_len;
                att_layout.leading=(unsigned)(buffer->om_data-buffer->om_databuf)-att_layout.header;
            }
            if (att_layout.bytes && (!buffer || (audio &&
                (small_free<4 || OS_MBUF_TRAILINGSPACE(buffer)<n+VIBE_BLE_HEADER)))) {
                unsigned required=att_layout.bytes+1u;
                struct os_mbuf *wide=required<=UINT16_MAX ?
                    os_msys_get_pkthdr((uint16_t)required,att_layout.header-sizeof(struct os_mbuf_pkthdr)):NULL;
                if (wide) {
                    uint32_t critical;OS_ENTER_CRITICAL(critical);
                    unsigned wide_free=wide->om_omp->omp_pool->mp_num_free;
                    OS_EXIT_CRITICAL(critical);
                    if (wide->om_omp->omp_databuf_len>att_layout.bytes &&
                        OS_MBUF_TRAILINGSPACE(wide)>=att_layout.leading+n+VIBE_BLE_HEADER &&
                        vibe_ble_tx_frame_admit((unsigned)os_msys_num_free(),wide_free,audio,offset==0,total,(unsigned)n)) {
                        wide->om_data+=att_layout.leading;
                        if (buffer) os_mbuf_free_chain(buffer);
                        buffer=wide;
                    } else os_mbuf_free_chain(wide);
                }
            }
            free_total=(unsigned)os_msys_num_free();min_pool=UINT16_MAX;
            if (buffer) {
                /* ATT headroom makes a full-MTU value spill into another small
                 * mbuf. Fit this fragment to the allocated block instead of
                 * consuming two blocks while the other pool remains free. */
                uint32_t critical;OS_ENTER_CRITICAL(critical);
                min_pool=buffer->om_omp->omp_pool->mp_num_free;
                OS_EXIT_CRITICAL(critical);
                unsigned space=OS_MBUF_TRAILINGSPACE(buffer);
                if (space>VIBE_BLE_HEADER && n>space-VIBE_BLE_HEADER) n=space-VIBE_BLE_HEADER;
                frame[0]=0x56;frame[1]=id;frame[2]=(offset==0?1:0)|(offset+n==total?2:0);
                frame[3]=total&255;frame[4]=total>>8;frame[5]=offset&255;frame[6]=offset>>8;
                for (size_t i=0;i<n;i++) frame[VIBE_BLE_HEADER+i]=(offset+i)==0 ? type : ((const uint8_t *)bytes)[offset+i-1];
                if (space>VIBE_BLE_HEADER && vibe_ble_tx_frame_admit(free_total,min_pool,audio,offset==0,total,(unsigned)n) &&
                    os_mbuf_append(buffer,frame,n+VIBE_BLE_HEADER)==0) {
                    result=ble_gatts_notify_custom(conn,tx_handle,buffer); /* Consumes buffer on every outcome. */
                    if (!result || (result!=BLE_HS_ENOMEM && result!=BLE_HS_EAGAIN)) break;
                } else os_mbuf_free_chain(buffer);
            } else min_pool=0;
            if (background || !vibe_ble_tx_wait(began,vibe_now(),audio)) break;
            vTaskDelay(pdMS_TO_TICKS(2)?pdMS_TO_TICKS(2):1);
        } while (!background && vibe_ble_tx_wait(began,vibe_now(),audio));
        if (result) {
            /* No console I/O while streaming or holding the send mutex. USB
             * host drainage is unrelated to radio capacity. Report at voice end. */
            if (audio) {
                ++voice_pressure.failures;voice_pressure.free_total=free_total;
                if(offset)++voice_pressure.partial;
                voice_pressure.pool=min_pool;voice_pressure.result=result;
            }
            if (background && result!=BLE_HS_ENOMEM && result!=BLE_HS_EAGAIN) voice_cursor.active=false;
            int submitted_prefix=background && voice_cursor.active && offset>initial_offset;
            xSemaphoreGive(tx_mutex);return submitted_prefix?1:-1;
        }
        offset+=n;
        if (background) voice_cursor.offset=offset;
        /* The next fragment must pass the same pool admission. Wait only under
         * pressure; imposing a per-fragment delay would hurt small-MTU peers. */
    }
    if (background) voice_cursor.active=false;
    xSemaphoreGive(tx_mutex);return 0;
}

int vibe_ble_send_to(uint16_t conn,unsigned char type,const void *bytes,size_t length) {
    return send_to(conn,type,bytes,length,false,0,0);
}
int vibe_ble_voice_submit(uint16_t conn,unsigned char type,const void *bytes,size_t length,
                           uint64_t session,uint32_t sequence) {
    if ((type!=1 && type!=6) || !session) return -1;
    return send_to(conn,type,bytes,length,true,session,sequence);
}
void vibe_ble_voice_report(void) {
    if (!tx_mutex) return;
    xSemaphoreTake(tx_mutex,portMAX_DELAY);
    unsigned failures=voice_pressure.failures,free_total=voice_pressure.free_total,pool=voice_pressure.pool;
    int result=voice_pressure.result;xSemaphoreGive(tx_mutex);
    if (failures) ESP_LOGI("vibe_ble","Voice pressure cumulative failures=%u last_result=%d free=%u pool=%u",failures,result,free_total,pool);
}
vibe_ble_voice_pressure_t vibe_ble_voice_pressure(void) {
    if (!tx_mutex) return (vibe_ble_voice_pressure_t){0};
    xSemaphoreTake(tx_mutex,portMAX_DELAY);
    vibe_ble_voice_pressure_t result=voice_pressure;
    xSemaphoreGive(tx_mutex);return result;
}
