#include "voice_capture.h"
#include "voice_uplink.h"
#include "vibe.h"
#include "adpcm.h"
#include "bsp_audio.h"
#include "esp_random.h"
#include "freertos/task.h"
#include <stdio.h>

enum { RELEASED=0, LINK_LOST=1, LIMIT=2, SOURCE_ERROR=3, REPLACED=4,
       NO_MEMORY=5, READY_TIMEOUT=6, COMPUTER_CANCELLED=7 };

static unsigned state_reason(uint32_t request, uint64_t started, bool v2,
                              uint64_t session, uint32_t owner) {
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    bool replaced=v_model.voice_request!=request;
    bool ready=v_model.voice_ready,recording=v_model.recording;
    xSemaphoreGive(v_mutex);
    if (replaced) return REPLACED;
    if (!ready || vibe_network_generation()!=owner) return LINK_LOST;
    if (v2 && vibe_voice_uplink_cancelled(session,owner)) return COMPUTER_CANCELLED;
    if (!recording) return RELEASED;
    if (vibe_now()-started>=V_VOICE_RECORD_MS) return LIMIT;
    return UINT32_MAX;
}

static bool prepare_input(int gain) {
    bool ready=bsp_audio_init()==ESP_OK && bsp_audio_wake()==ESP_OK &&
        bsp_audio_set_format(16000,16,1)==ESP_OK &&
        bsp_audio_set_input_gain(gain)==ESP_OK;
    if (ready) bsp_audio_set_volume(0);
    return ready;
}

static unsigned wait_for_ready(const char *start, uint64_t session, uint32_t owner,
                               uint32_t request, uint64_t began) {
    uint64_t deadline=vibe_now()+V_VOICE_READY_MS, resend=vibe_now()+V_VOICE_START_RETRY_MS;
    while (!vibe_voice_uplink_is_ready(session,owner)) {
        unsigned reason=state_reason(request,began,true,session,owner);
        if (reason!=UINT32_MAX) return reason;
        uint64_t now=vibe_now();
        if (now>=deadline) return READY_TIMEOUT;
        if (now>=resend) {vibe_send_json_owned(start,owner,V_DELIVERY_CRITICAL);resend=now+V_VOICE_START_RETRY_MS;}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return UINT32_MAX;
}

void vibe_voice_capture(int16_t *pcm) {
    /* Only copy capture inputs. The full application status contains weather,
     * UI and radio data that do not belong on this worker's small stack. */
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    bool active=v_model.recording && v_model.voice_ready;
    uint32_t request=v_model.voice_request;
    int gain=v_prefs.mic_gain;
    bool v2=v_status.voice_v2,repair=v_status.voice_repair;
    bool handshake=v_status.voice_handshake,interleave=v_status.voice_interleave;
    bool feedback=v_status.voice_feedback;
    xSemaphoreGive(v_mutex);
    if (!active) return;

    const uint32_t owner=vibe_network_generation();
    const vibe_voice_counters_t before=vibe_voice_counters();
    uint64_t session=((uint64_t)esp_random()<<32)|esp_random();
    if (!session) session=1;
    uint64_t began=vibe_now(), previous_read=0;
    unsigned frames=0, rejected=0, capture_gap=0, enqueue_gap=0, reason=SOURCE_ERROR;
    bool opened=false;
    char event[768];

    if (prepare_input(gain)) {
        vibe_voice_mode_t mode=!v2 ? V_VOICE_LEGACY:interleave ? V_VOICE_STREAM:V_VOICE_V2;
        unsigned history=vibe_voice_begin(session,owner,mode,feedback);
        if (history) {
            if (v2) snprintf(event,sizeof(event),
                "{\"event\":\"voice.start\",\"audio\":\"ima_adpcm\",\"voiceVersion\":2,"
                "\"voiceSession\":\"%016llx\",\"voiceRequest\":%lu,\"voiceRepair\":%u,"
                "\"voiceLinks\":%u,\"ready\":%s,\"voiceFeedback\":%u}",
                (unsigned long long)session,(unsigned long)request,
                repair?history:0,vibe_voice_links(owner),handshake?"true":"false",feedback?1:0);
            else snprintf(event,sizeof(event),"{\"event\":\"voice.start\",\"audio\":\"ima_adpcm\"}");
            vibe_send_json_owned(event,owner,V_DELIVERY_CRITICAL);
            opened=true;
            if (v2 && handshake) reason=wait_for_ready(event,session,owner,request,began);
            else {vTaskDelay(pdMS_TO_TICKS(80));reason=UINT32_MAX;}

            adpcm_state_t codec;
            adpcm_state_reset(&codec);
            uint8_t packet[VIBE_VOICE_BODY];
            size_t header=VIBE_VOICE_HEADER;
            while (reason==UINT32_MAX) {
                reason=state_reason(request,began,v2,session,owner);
                if (reason!=UINT32_MAX) break;
                uint64_t now=vibe_now();
                if (previous_read && now-previous_read>capture_gap) capture_gap=(unsigned)(now-previous_read);
                previous_read=now;
                if (bsp_audio_read(pcm,ADPCM_BLOCK_SAMPLES*2)!=ESP_OK) {reason=SOURCE_ERROR;break;}
                size_t length=adpcm_encode_block(&codec,pcm,ADPCM_BLOCK_SAMPLES,
                                                packet+header,ADPCM_BLOCK_BYTES);
                length=vibe_voice_encode(session,frames,packet+header,length,packet,sizeof(packet));
                ++frames;
                now=vibe_now();
                if (!length || vibe_send_voice(packet,length,owner)) ++rejected;
                uint64_t elapsed=vibe_now()-now;
                if (elapsed>enqueue_gap) enqueue_gap=(unsigned)elapsed;
            }
        } else reason=vibe_network_generation()==owner ? NO_MEMORY:LINK_LOST;
    }

    /* Complete only our own request, before any diagnostics or asynchronous
     * failure report. A retry may already be waiting on the audio worker. */
    bool failed=reason==SOURCE_ERROR || reason==NO_MEMORY || reason==READY_TIMEOUT;
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    if (v_model.voice_request==request) {
        v_model.recording=false;
        v_status.audio_fault=failed;
    }
    v_status.drops+=rejected;
    xSemaphoreGive(v_mutex);

    if (opened) {
        vibe_voice_end(session,owner);
        if (v2) {
            snprintf(event,sizeof(event),
                "{\"event\":\"voice.end\",\"voiceVersion\":2,\"voiceSession\":\"%016llx\","
                "\"voiceRequest\":%lu,\"frames\":%u,\"stop\":%u,\"atMs\":%llu}",
                (unsigned long long)session,(unsigned long)request,frames,reason,(unsigned long long)vibe_now());
            vibe_send_json_owned(event,owner,V_DELIVERY_CRITICAL);
            vibe_voice_counters_t after=vibe_voice_counters();
            int n=vibe_voice_terminal(event,sizeof(event),session,frames,(unsigned)(vibe_now()-began),
                                      capture_gap,enqueue_gap,reason,&before,&after);
            if (n>0 && (size_t)n<sizeof(event)) vibe_send_json_owned(event,owner,V_DELIVERY_BOTH);
        } else vibe_send_json_owned("{\"event\":\"voice.end\"}",owner,V_DELIVERY_CRITICAL);
    }
    if (failed) {
        vibe_msg_t message={.type=V_MSG_AUDIO_FAIL,.link_generation=request};
        xQueueSend(v_queue,&message,0);
    }
}
