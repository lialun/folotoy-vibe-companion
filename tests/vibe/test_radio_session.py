"""Run the real bounded playback session with a threaded reader and faulting decoder."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
headers={
 'esp_timer.h': '''#pragma once
#include <chrono>
#include <cstdint>
inline int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
''',
 'vibe.h': '''#pragma once
#include <stddef.h>
#include <stdint.h>
struct vibe_prefs_t { unsigned volume; };
void vibe_snapshot(void *,vibe_prefs_t *,void *);
''',
 'bsp_audio.h': '''#pragma once
#include <stddef.h>
#include <stdint.h>
using esp_err_t=int;
#define ESP_OK 0
esp_err_t bsp_audio_set_format(uint32_t,uint8_t,uint8_t);
void bsp_audio_set_volume(uint8_t);
esp_err_t bsp_audio_write(const void *,size_t);
''',
 'esp_http_client.h': '''#pragma once
using esp_err_t=int;using esp_http_client_handle_t=void*;struct esp_http_client_event_t;
''',
 'freertos/task.h': '''#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#define pdMS_TO_TICKS(x) (x)
using StackType_t=uint8_t;struct StaticTask_t { std::thread *thread=nullptr;std::mutex mutex;std::condition_variable cv;unsigned count=0; };using TaskHandle_t=StaticTask_t*;
TaskHandle_t xTaskCreateStatic(void (*)(void*),const char*,unsigned,void*,unsigned,StackType_t*,StaticTask_t*);
constexpr int pdTRUE=1;
TaskHandle_t xTaskGetCurrentTaskHandle();
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
void vTaskDelay(unsigned);void vTaskSuspend(void*);void vTaskDelete(TaskHandle_t);
''',
 'esp_log.h': '''#pragma once
#include <cstdio>
#define ESP_LOGI(tag,...) do {(void)tag;printf(__VA_ARGS__);puts("");}while(0)
#define ESP_LOGE ESP_LOGI
#define ESP_LOGW ESP_LOGI
''',
 'simple_dec/esp_audio_simple_dec.h': '''#pragma once
#include <stdint.h>
enum esp_audio_simple_dec_type_t { ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,ESP_AUDIO_SIMPLE_DEC_TYPE_TS };
using esp_audio_err_t=int;using esp_audio_simple_dec_handle_t=void*;
constexpr int ESP_AUDIO_ERR_OK=0,ESP_AUDIO_ERR_MEM_LACK=-2,ESP_AUDIO_ERR_NOT_SUPPORT=-7;
struct esp_audio_simple_dec_cfg_t {esp_audio_simple_dec_type_t dec_type;};
struct esp_audio_simple_dec_raw_t {uint8_t *buffer;uint32_t len,consumed;};
struct esp_audio_simple_dec_out_t {uint8_t *buffer;uint32_t len,needed_size,decoded_size;};
struct esp_audio_simple_dec_info_t {unsigned channel,bits_per_sample,sample_rate;};
int esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t*,void**);
int esp_audio_simple_dec_process(void*,esp_audio_simple_dec_raw_t*,esp_audio_simple_dec_out_t*);
int esp_audio_simple_dec_get_info(void*,esp_audio_simple_dec_info_t*);
void esp_audio_simple_dec_close(void*);
''',
 'simple_dec/esp_audio_simple_dec_default.h': '''#pragma once
int esp_audio_simple_dec_register_default();void esp_audio_simple_dec_unregister_default();
''',
 'decoder/esp_audio_dec_default.h': '''#pragma once
#include "simple_dec/esp_audio_simple_dec.h"
constexpr int ESP_AUDIO_TYPE_MP3=1,ESP_AUDIO_TYPE_AAC=2;
struct esp_audio_dec_ops_t {int (*open)(void*,uint32_t,void**);int (*decode)();int (*reset)();int (*close)(void*);};
const esp_audio_dec_ops_t *esp_audio_dec_get_ops(int);
void esp_audio_dec_unregister(int);
int esp_audio_dec_register(int,const esp_audio_dec_ops_t *);
int esp_audio_dec_register_default();void esp_audio_dec_unregister_default();
''',
}
source=r'''
#include "radio_source.h"
#include "catalog_service.h"
#include "decoder/esp_audio_dec_default.h"
#include "vibe.h"
#include "freertos/task.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <chrono>
#include <cstdio>
static std::atomic<bool> cancelled{false};
static std::atomic<int> in_read{0};
static int writes,frames,bytes,fail_mode,readers,decoders,registrations,errors,frame_decoders,consumed_bytes;
static unsigned applied_volume,volume_changes;
static void *adopted_frame=nullptr;
static const esp_audio_dec_ops_t *active_ops=nullptr;
RadioSource::~RadioSource() { assert(in_read==0); }
bool RadioSource::open(const char*) {bytes=0;type_=fail_mode==19?ESP_AUDIO_SIMPLE_DEC_TYPE_AAC:fail_mode>=11?ESP_AUDIO_SIMPLE_DEC_TYPE_TS:ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;return fail_mode!=1;}
int RadioSource::read(uint8_t *out,size_t capacity) {
 ++in_read; std::this_thread::sleep_for(std::chrono::milliseconds(1));
 if(fail_mode==14)::cancelled=true;
 int limit=fail_mode==15?1024:fail_mode==18?131072:fail_mode==21?0:32768;
 int n=bytes>=limit?0:int(capacity);std::memset(out,0x55,n);bytes+=n;if(!n)finished_=true;
 --in_read;return n;
}
static thread_local TaskHandle_t current=nullptr;
TaskHandle_t xTaskCreateStatic(void (*fn)(void*),const char*,unsigned,void*ctx,unsigned,StackType_t*,StaticTask_t*t) {
 if(fail_mode==20)return nullptr;
 ++readers;t->thread=new std::thread([=]{current=t;fn(ctx);});return t;
}
void vTaskDelete(TaskHandle_t t) {t->thread->join();delete t->thread;--readers;}
TaskHandle_t xTaskGetCurrentTaskHandle(){static StaticTask_t root;if(!current)current=&root;return current;}
void xTaskNotifyGive(TaskHandle_t t){std::lock_guard<std::mutex> lock(t->mutex);++t->count;t->cv.notify_one();}
unsigned ulTaskNotifyTake(int clear,unsigned timeout){auto*t=xTaskGetCurrentTaskHandle();std::unique_lock<std::mutex> lock(t->mutex);t->cv.wait_for(lock,std::chrono::milliseconds(timeout),[=]{return t->count>0;});unsigned n=t->count;if(clear)t->count=0;else if(n)--t->count;return n;}
void vTaskSuspend(void*) {}
void vTaskDelay(unsigned) {std::this_thread::sleep_for(std::chrono::milliseconds(1));}
int frame_open(void*,uint32_t,void**out){assert(frame_decoders==0);if(fail_mode>=11)assert(readers==0);if(fail_mode==5)return ESP_AUDIO_ERR_MEM_LACK;++frame_decoders;*out=(void*)2;return 0;}
int frame_close(void*p){assert(p==(void*)2 && frame_decoders==1);--frame_decoders;return 0;}
const esp_audio_dec_ops_t base_ops{frame_open,nullptr,nullptr,frame_close};
const esp_audio_dec_ops_t *esp_audio_dec_get_ops(int){return active_ops;}
void esp_audio_dec_unregister(int){active_ops=nullptr;}
int esp_audio_dec_register(int,const esp_audio_dec_ops_t *ops){if(fail_mode==7)return ESP_AUDIO_ERR_MEM_LACK;active_ops=ops;return 0;}
int esp_audio_dec_register_default(){++registrations;active_ops=&base_ops;return fail_mode==9?ESP_AUDIO_ERR_MEM_LACK:0;}
void esp_audio_dec_unregister_default(){--registrations;active_ops=nullptr;}
int esp_audio_simple_dec_register_default(){++registrations;return fail_mode==10?ESP_AUDIO_ERR_MEM_LACK:0;}
void esp_audio_simple_dec_unregister_default(){--registrations;}
int esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t*,void**out){if(fail_mode==6)return ESP_AUDIO_ERR_MEM_LACK;*out=(void*)1;++decoders;return 0;}
void esp_audio_simple_dec_close(void*){--decoders;if(adopted_frame){active_ops->close(adopted_frame);adopted_frame=nullptr;}}
int esp_audio_simple_dec_get_info(void*,esp_audio_simple_dec_info_t*i){*i={2,16,44100};if(fail_mode==3)i->channel=7;return 0;}
int esp_audio_simple_dec_process(void*,esp_audio_simple_dec_raw_t *raw,esp_audio_simple_dec_out_t*out){
 ++frames;if(fail_mode==2)return ESP_AUDIO_ERR_MEM_LACK;
 if(fail_mode==12)return ESP_AUDIO_ERR_MEM_LACK;
 if(!adopted_frame)assert(active_ops->open(nullptr,0,&adopted_frame)==0);
 assert(frame_decoders==1);
 raw->consumed=fail_mode==13 && frames==1?raw->len/2:raw->len;
 if((fail_mode==24 && frames==1) || (fail_mode==25 && frames==3) ||
    (fail_mode==26 && frames==1) || (fail_mode==27 && frames==3) || (fail_mode==28 && frames>=3))raw->consumed=0;
 consumed_bytes+=raw->consumed;
 assert(out->len==((fail_mode>=11 && !readers) || fail_mode==29?8192:4608));std::memset(out->buffer,0,out->len);auto*s=reinterpret_cast<int16_t*>(out->buffer);s[0]=300;s[1]=-100;s[2]=600;s[3]=-200;
 out->decoded_size=fail_mode==8?4608:8;
 if(fail_mode==29)out->decoded_size=8192;
 if(fail_mode==16 || fail_mode==18)out->decoded_size=0;
 if(fail_mode==17){raw->consumed=0;out->decoded_size=0;}
 if((fail_mode==26 && frames==1) || (fail_mode==27 && frames==3) || (fail_mode==28 && frames>=3))out->decoded_size=0;
 if(fail_mode==22)raw->consumed=raw->len+1;
 if(fail_mode==23)out->decoded_size=out->len+1;
 return 0;
}
extern "C" int bsp_audio_set_format(uint32_t rate,uint8_t bits,uint8_t channels){assert(readers==0&&frame_decoders==1);assert(rate==44100&&bits==16&&channels==1);return 0;}
extern "C" void bsp_audio_set_volume(uint8_t v){assert(v==60||v==0||(fail_mode==30&&v==25));applied_volume=v;++volume_changes;}
extern "C" int bsp_audio_write(const void*pcm,size_t n){auto*s=static_cast<const int16_t*>(pcm);assert(n==(fail_mode==8?2304:fail_mode==29?4096:4)&&s[0]==100&&s[1]==200);if(fail_mode==30)assert(applied_volume==(writes?25:60));++writes;if(fail_mode==4)cancelled=true;return 0;}
void vibe_snapshot(void*,vibe_prefs_t*p,void*){p->volume=fail_mode==30&&writes?25:60;}
extern "C" void vibe_radio_played(const char*){}
extern "C" void vibe_radio_status(unsigned state,const char*){if(state==V_RADIO_ERROR)++errors;}
bool cancel(){return cancelled.load();}
int main(){
 for(int repeat=0;repeat<3;repeat++)for(int mode=0;mode<31;mode++){
  writes=frames=errors=consumed_bytes=0;volume_changes=0;fail_mode=mode;cancelled=false;
  const auto result=vibe_radio_session("http://fixture/radio",cancel);
  const bool played=result==V_RADIO_SESSION_PLAYED;
  assert(readers==0&&decoders==0&&registrations==0&&in_read==0&&frame_decoders==0&&!adopted_frame);
  if(mode==0 || mode==8)assert(played&&writes>1);
  if(mode==1)assert(!played&&writes==0&&errors>0);
  if(mode==2)assert(!played&&writes==0&&errors>0&&result==V_RADIO_SESSION_NO_MEMORY);
  if(mode==3)assert(!played&&writes==0&&errors>0);
  if(mode==4)assert(played&&writes==1&&cancelled);
  if(mode>=5 && mode<=7)assert(result==V_RADIO_SESSION_NO_MEMORY&&writes==0&&errors>0);
  if(mode==9 || mode==10)assert(result==V_RADIO_SESSION_NO_MEMORY&&writes==0&&errors>0);
  if(mode==11 || mode==13 || mode==15 || (mode>=24 && mode<=27) || mode==29)assert(played&&writes>0&&consumed_bytes==bytes);
  if(mode==28)assert(played&&writes==2&&errors>0&&frames==10);
  if(mode==12)assert(result==V_RADIO_SESSION_NO_MEMORY&&writes==0&&errors>0);
  if(mode==14)assert(!played&&cancelled&&writes==0);
  if(mode==16 || mode==17)assert(!played&&writes==0&&errors>0);
  if(mode==18)assert(!played&&writes==0&&errors>0&&bytes==65536);
  if(mode==19)assert(played&&writes>1&&consumed_bytes==bytes);
  if(mode==20)assert(result==V_RADIO_SESSION_NO_MEMORY&&writes==0&&errors>0);
  if(mode>=21 && mode<=23)assert(!played&&writes==0&&errors>0);
  if(mode==30)assert(played&&writes>1&&consumed_bytes==bytes&&volume_changes==2&&applied_volume==25);
 }
 puts("Real radio session: MP3/AAC/TS, primed PCM/input preservation, bounded malformed input, partial registry rollback, reader join and repeated cleanup PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='vibe-radio-session-') as directory:
    tmp=Path(directory)
    for name,content in headers.items():
        p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
    (tmp/'test.cc').write_text(source)
    binary=tmp/'test'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread',
                    '-I'+str(tmp),'-I'+str(ROOT/'tests/vibe/vendor/cjson'),'-I'+str(ROOT/'main/vibe/radio'),str(tmp/'test.cc'),
                    str(ROOT/'main/vibe/radio/session.cc'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=20)
