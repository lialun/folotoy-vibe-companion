extern "C" {
#include "bsp_audio.h"
}
#include "vibe.h"
#include "catalog_service.h"
#include "radio_source.h"
#include "radio_stream_buffer.h"
#include "radio_memory.h"
#include "decoder/esp_audio_dec_default.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif

namespace {
constexpr size_t input_size=radio_stream::Workspace::input_size,stack_size=radio_stream::Workspace::stack_size;
constexpr char tag[]="vibe_radio";
void report_heap(const char *phase) {
#ifdef ESP_PLATFORM
    ESP_LOGI(tag,"Heap %s: free=%u largest=%u minimum=%u",phase,(unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)esp_get_minimum_free_heap_size());
#else
    (void)phase;
#endif
}
// The single audio worker owns decoder registration. Open the real frame
// decoder before playback buffers consume its largest contiguous SRAM block.
// SimpleDecoder's parser subsequently adopts that same handle; no dummy heap
// reservation and no second MP3/AAC decoder are needed.
const esp_audio_dec_ops_t *reserved_base=nullptr;
esp_audio_dec_ops_t reserved_ops{};
void *reserved_frame=nullptr;
esp_audio_err_t adopt_frame(void *cfg,uint32_t size,void **handle) {
    if (reserved_frame) { *handle=reserved_frame;reserved_frame=nullptr;return ESP_AUDIO_ERR_OK; }
    return reserved_base->open(cfg,size,handle);
}
esp_audio_err_t reserve_decoder(esp_audio_simple_dec_type_t type) {
    if (type!=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3 && type!=ESP_AUDIO_SIMPLE_DEC_TYPE_AAC) return ESP_AUDIO_ERR_OK;
    const auto codec=type==ESP_AUDIO_SIMPLE_DEC_TYPE_MP3?ESP_AUDIO_TYPE_MP3:ESP_AUDIO_TYPE_AAC;
    reserved_base=esp_audio_dec_get_ops(codec);
    if (!reserved_base) return ESP_AUDIO_ERR_NOT_SUPPORT;
    const auto result=reserved_base->open(nullptr,0,&reserved_frame);
    if (result!=ESP_AUDIO_ERR_OK) return result;
    reserved_ops=*reserved_base;reserved_ops.open=adopt_frame;
    esp_audio_dec_unregister(codec);
    return esp_audio_dec_register(codec,&reserved_ops);
}
void release_reserved_decoder() {
    if (reserved_frame) { reserved_base->close(reserved_frame);reserved_frame=nullptr; }
    reserved_base=nullptr;
}
bool prime_decoder(RadioSource &source,esp_audio_simple_dec_handle_t decoder,
                     radio_stream::Workspace &workspace,bool (*cancel)(),
                     radio_stream::IcyFilter &icy,esp_audio_simple_dec_raw_t &remaining,unsigned &pcm,bool &out_of_memory) {
    const int64_t deadline=esp_timer_get_time()+15000000;
    unsigned inspected=0;
    while (!cancel() && inspected<65536 && esp_timer_get_time()<deadline) {
        const int bytes=source.read(workspace.input,input_size);
        if (bytes<=0) break;
        inspected+=bytes;
        esp_audio_simple_dec_raw_t raw{};raw.buffer=workspace.input;raw.len=icy.filter(workspace.input,bytes);
        unsigned stalled=0;
        while (raw.len && !cancel()) {
            esp_audio_simple_dec_out_t frame{};frame.buffer=workspace.output;frame.len=workspace.output_capacity;
            raw.consumed=0;
            const auto error=esp_audio_simple_dec_process(decoder,&raw,&frame);
            if (error!=ESP_AUDIO_ERR_OK || raw.consumed>raw.len || frame.decoded_size>frame.len) {
                out_of_memory=error==ESP_AUDIO_ERR_MEM_LACK;
                ESP_LOGE(tag,"Container preparation failed: %d needed=%lu",error,(unsigned long)frame.needed_size);
                vibe_radio_status(V_RADIO_ERROR,out_of_memory?"解码内存不足":"解码失败");return false;
            }
            raw.buffer+=raw.consumed;raw.len-=raw.consumed;
            if (frame.decoded_size) {
                remaining=raw;remaining.consumed=0;pcm=frame.decoded_size;
                ESP_LOGI(tag,"Codec ready before FIFO: inspected=%u",inspected);
                return true;
            }
            // Allow bounded parser state transitions without new input/output.
            if (!raw.consumed && ++stalled>=8) {
                ESP_LOGE(tag,"Decoder preparation made no progress");
                vibe_radio_status(V_RADIO_ERROR,"解码失败");return false;
            }
            if (raw.consumed) stalled=0;
        }
    }
    if (!cancel()) vibe_radio_status(V_RADIO_ERROR,"连接失败");
    return false;
}
struct Reader {
    RadioSource &source;
    radio_stream::Buffer buffer;
    radio_stream::IcyFilter icy;
    bool (*cancel)();
    TaskHandle_t consumer;
    std::atomic<bool> stop{false},finished{false},failed{false};
    std::atomic<unsigned> network_bytes{0},read_peak_us{0},last_data_ms{0};
    Reader(RadioSource &s,radio_stream::Workspace &w,bool (*c)(),const radio_stream::IcyFilter &filter)
        : source(s),buffer(w.fifo,w.fifo_block_size,w.fifo_count),icy(filter),cancel(c),consumer(xTaskGetCurrentTaskHandle()) {}
};
void read_stream(void *opaque) {
    auto &r=*static_cast<Reader *>(opaque);
    while (!r.stop.load(std::memory_order_acquire) && !r.cancel()) {
        size_t room; uint8_t *target=r.buffer.write_data(room);
        if (!room) { ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(250)); continue; }
        const int64_t started=esp_timer_get_time();
        const int n=r.source.read(target,std::min(room,input_size));
        const int64_t ended=esp_timer_get_time();
        const unsigned elapsed=std::min<int64_t>(ended-started,UINT32_MAX);
        unsigned previous=r.read_peak_us.load();
        while (elapsed>previous && !r.read_peak_us.compare_exchange_weak(previous,elapsed)) {}
        if (n<=0) {
            const bool cancelled=r.stop.load(std::memory_order_acquire) || r.cancel();
            ESP_LOGI(tag,"Reader ended: result=%d received=%u queued=%u cancelled=%d",n,r.network_bytes.load(),(unsigned)r.buffer.available(),int(cancelled));
            r.failed=n<0 && !cancelled;break;
        }
        r.last_data_ms.store(static_cast<unsigned>(ended/1000));r.network_bytes+=n;
        r.buffer.commit(r.icy.filter(target,n));
        xTaskNotifyGive(r.consumer);
    }
    r.finished.store(true,std::memory_order_release);
    xTaskNotifyGive(r.consumer);
    vTaskSuspend(nullptr); // The owning session joins and deletes this static task.
}
}
/* Session owns FIFO/HTTP/decoder/I2S. Reader is joined before freeing any data. */
extern "C" vibe_radio_result_t vibe_radio_session(const char *url,bool (*cancel)(void)) {
    report_heap("session");
    radio_stream::Workspace workspace;
    struct Context { bool (*cancel)(void); } context{cancel};
    RadioSource source([](void *p){ return static_cast<Context *>(p)->cancel(); },&context);
    esp_audio_simple_dec_handle_t decoder=nullptr;
    bool registered=false,simple_registered=false,played=false,out_of_memory=false;
    do {
        if (!source.open(url)) { if (cancel()) break;out_of_memory=source.oom();vibe_radio_status(V_RADIO_ERROR,out_of_memory?"播放内存不足":"连接失败"); break; }
        report_heap("source-open");
        if (cancel()) break;
        // Registration can fail after installing some entries. Always roll back
        // an attempted registration, including that partial-success case.
        registered=true;
        auto result=esp_audio_dec_register_default();
        if (result==ESP_AUDIO_ERR_OK) result=reserve_decoder(source.type());
        if (result!=ESP_AUDIO_ERR_OK) {
            out_of_memory=result==ESP_AUDIO_ERR_MEM_LACK;
            ESP_LOGE(tag,"Frame decoder preparation failed: %d",result);
            vibe_radio_status(V_RADIO_ERROR,out_of_memory?"解码内存不足":"解码初始化失败");break;
        }
        simple_registered=true;
        result=esp_audio_simple_dec_register_default();
        if (result!=ESP_AUDIO_ERR_OK) {
            out_of_memory=result==ESP_AUDIO_ERR_MEM_LACK;
            ESP_LOGE(tag,"Decoder registration failed: %d",result);
            vibe_radio_status(V_RADIO_ERROR,out_of_memory?"解码内存不足":"解码初始化失败");break;
        }
        esp_audio_simple_dec_cfg_t cfg{}; cfg.dec_type=source.type();
        const auto opened=esp_audio_simple_dec_open(&cfg,&decoder);
        if (opened!=ESP_AUDIO_ERR_OK) { out_of_memory=opened==ESP_AUDIO_ERR_MEM_LACK;ESP_LOGE(tag,"Decoder open failed: %d",opened);vibe_radio_status(V_RADIO_ERROR,out_of_memory?"解码内存不足":"解码初始化失败"); break; }
        if (!workspace.allocate_decode(std::malloc,source.type()==ESP_AUDIO_SIMPLE_DEC_TYPE_MP3)) { out_of_memory=true;vibe_radio_status(V_RADIO_ERROR,"播放内存不足");break; }
        esp_audio_simple_dec_raw_t primed_raw{};
        unsigned primed_pcm=0;
        radio_stream::IcyFilter icy(source.icy_interval());
        if (!prime_decoder(source,decoder,workspace,cancel,icy,primed_raw,primed_pcm,out_of_memory)) break;
        workspace.compact_output(primed_pcm); // Failure retains the larger block.
        esp_audio_simple_dec_info_t info{};
        if (esp_audio_simple_dec_get_info(decoder,&info)!=ESP_AUDIO_ERR_OK || info.channel<1 || info.channel>2 || info.bits_per_sample!=16 ||
            info.sample_rate<8000 || info.sample_rate>48000 || bsp_audio_set_format(info.sample_rate,16,1)!=ESP_OK) {
            vibe_radio_status(V_RADIO_ERROR,"格式不支持");break;
        }
        report_heap("decoder-ready");
        if (cancel()) break;
#ifdef ESP_PLATFORM
        if (!workspace.plan_stream(esp_get_free_heap_size())) {
            out_of_memory=true;vibe_radio_status(V_RADIO_ERROR,"播放内存不足");break;
        }
#endif
        if (!workspace.allocate_stream()) { out_of_memory=true;vibe_radio_status(V_RADIO_ERROR,"播放内存不足");break; }
#ifdef ESP_PLATFORM
        if (!workspace.retain_reserve(esp_get_free_heap_size())) {
            out_of_memory=true;vibe_radio_status(V_RADIO_ERROR,"播放内存不足");break;
        }
#endif
        report_heap("workspace-ready");
        const size_t fifo_size=workspace.fifo_bytes(),output_size=workspace.output_capacity;
        ESP_LOGI(tag,"Workspace: fifo=%u output=%u",(unsigned)fifo_size,(unsigned)output_size);
        auto *input=workspace.input,*output=workspace.output;
        Reader reader(source,workspace,cancel,icy);
        StaticTask_t control;
        TaskHandle_t task=xTaskCreateStatic(read_stream,"radio_http",stack_size,&reader,5,
            reinterpret_cast<StackType_t *>(workspace.stack),&control);
        if (!task) { out_of_memory=true;vibe_radio_status(V_RADIO_ERROR,"读取任务启动失败");break; }
        bool buffering=true,first=true,failed=false;
        int last_volume=-1;
        const unsigned channels=info.channel;
        unsigned refills=0;
        ESP_LOGI(tag,"Playing %luHz/%u channels -> mono",(unsigned long)info.sample_rate,channels);
        int64_t report_at=esp_timer_get_time(),last_write_end=0;
        unsigned rx_at=0,decode_peak=0,pcm_gap_peak=0,fifo_min=fifo_size;
        vibe_radio_status(V_RADIO_BUFFERING,"正在缓冲");
        while (!cancel()) {
            const int64_t now=esp_timer_get_time();
            const bool finished=reader.finished.load(std::memory_order_acquire);
            const size_t available=reader.buffer.available();
            if (played) fifo_min=std::min<unsigned>(fifo_min,available);
            if (now-report_at>=30000000) {
                const unsigned rx=reader.network_bytes.load();
                ESP_LOGI(tag,"Stream 30s: rx=%u fifo=%u min_fifo=%u read_max_ms=%u decode_max_ms=%u pcm_gap_max_ms=%u refills=%u",
                    rx-rx_at,(unsigned)available,fifo_min,reader.read_peak_us.exchange(0)/1000,
                    decode_peak/1000,pcm_gap_peak/1000,refills);
                report_at=now;rx_at=rx;decode_peak=pcm_gap_peak=0;fifo_min=available;
            }
            if (finished && !available && !primed_pcm) break;
            if (buffering && available<(first?fifo_size*3/4:fifo_size/2) && !finished) {
                ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(250)); continue;
            }
            buffering=false; first=false;
            esp_audio_simple_dec_raw_t raw{};
            if (primed_pcm) raw=primed_raw;
            else {
                const size_t received=reader.buffer.read(input,input_size);
                xTaskNotifyGive(task);
                if (!received) {
                    ESP_LOGW(tag,"Buffer empty: received=%u data_idle_ms=%lu",reader.network_bytes.load(),
                        (unsigned long)(static_cast<unsigned>(now/1000)-reader.last_data_ms.load()));
                    buffering=true; ++refills; vibe_radio_status(V_RADIO_BUFFERING,"正在缓冲"); continue;
                }
                raw.buffer=input;raw.len=received;
            }
            unsigned stalled=0;
            while ((raw.len || primed_pcm) && !cancel()) {
                esp_audio_simple_dec_out_t frame{}; frame.buffer=output; frame.len=output_size;
                const bool primed=primed_pcm!=0;
                const int64_t decode_started=esp_timer_get_time();
                esp_audio_err_t error=ESP_AUDIO_ERR_OK;
                if (primed) { frame.decoded_size=primed_pcm;primed_pcm=0; }
                else { raw.consumed=0;error=esp_audio_simple_dec_process(decoder,&raw,&frame); }
                decode_peak=std::max<unsigned>(decode_peak,std::min<int64_t>(esp_timer_get_time()-decode_started,UINT32_MAX));
                if (error!=ESP_AUDIO_ERR_OK || (!primed && raw.consumed>raw.len) || frame.decoded_size>output_size) {
                    out_of_memory=error==ESP_AUDIO_ERR_MEM_LACK;
                    ESP_LOGE(tag,"Decode failed: %d needed=%lu",error,(unsigned long)frame.needed_size);
#ifdef ESP_PLATFORM
                    ESP_LOGE(tag,"Decoder heap: free=%u largest=%u",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
                    vibe_radio_status(V_RADIO_ERROR,error==ESP_AUDIO_ERR_MEM_LACK?"解码内存不足":"解码失败"); failed=true; break;
                }
                if (!primed) { raw.buffer+=raw.consumed; raw.len-=raw.consumed; }
                if (frame.decoded_size) {
                    auto *samples=reinterpret_cast<int16_t *>(output);
                    size_t count=frame.decoded_size/(2*channels);
                    if (channels==2) for (size_t i=0;i<count;i++) samples[i]=(int32_t(samples[2*i])+samples[2*i+1])/2;
                    vibe_prefs_t prefs; vibe_snapshot(nullptr,&prefs,nullptr);
                    if (last_volume!=int(prefs.volume)) { bsp_audio_set_volume(prefs.volume);last_volume=prefs.volume; }
                    const int64_t write_started=esp_timer_get_time();
                    if (last_write_end) pcm_gap_peak=std::max<unsigned>(pcm_gap_peak,std::min<int64_t>(write_started-last_write_end,UINT32_MAX));
                    if (bsp_audio_write(output,count*2)!=ESP_OK) { failed=true; break; }
                    last_write_end=esp_timer_get_time();
                    if (!played) {
#ifdef ESP_PLATFORM
                        ESP_LOGI(tag,"Playback heap: free=%u largest=%u",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
                        vibe_radio_status(V_RADIO_PLAYING,"");vibe_radio_played(url);
                    }
                    played=true;
                }
                // A container may drain a buffered audio frame without taking
                // more input. Preserve and resubmit those bytes; discarding
                // them corrupts the next TS/PES packet and AAC frame.
                if (!primed && !raw.consumed && !frame.decoded_size && ++stalled>=8) {
                    ESP_LOGE(tag,"Decoder made no progress: remaining=%lu",(unsigned long)raw.len);
                    vibe_radio_status(V_RADIO_ERROR,"解码失败");failed=true;break;
                }
                if (primed || raw.consumed || frame.decoded_size) stalled=0;
            }
            if (failed) break;
            if (played && !buffering) vibe_radio_status(V_RADIO_PLAYING,"");
        }
        if (cancel()) bsp_audio_set_volume(0);
        reader.stop.store(true,std::memory_order_release);
        xTaskNotifyGive(task);
        while (!reader.finished.load(std::memory_order_acquire)) ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(250));
        vTaskDelete(task);
        ESP_LOGI(tag,"Session stopped: refills=%u network_error=%d read_max_ms=%u decode_max_ms=%u pcm_gap_max_ms=%u",
            refills,int(reader.failed.load()),reader.read_peak_us.load()/1000,decode_peak/1000,pcm_gap_peak/1000);
    } while (false);
    if (decoder) esp_audio_simple_dec_close(decoder);
    release_reserved_decoder();
    if (simple_registered) esp_audio_simple_dec_unregister_default();
    if (registered) esp_audio_dec_unregister_default();
    return out_of_memory?V_RADIO_SESSION_NO_MEMORY:played?V_RADIO_SESSION_PLAYED:V_RADIO_SESSION_FAILED;
}
