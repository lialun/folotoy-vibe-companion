"""Exercise production HTTP/HLS transport against deterministic network fixtures."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    'esp_http_client.h': r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int;
constexpr int ESP_OK=0,HTTP_EVENT_ON_HEADER=1,ESP_ERR_HTTP_EAGAIN=0x7007;
constexpr int HTTP_TLS_DYN_BUF_RX_STATIC=1;
struct Http;
using esp_http_client_handle_t=Http*;
struct esp_http_client_event_t {int event_id;void* user_data;char* header_key;char* header_value;};
struct esp_http_client_config_t {const char* url;int timeout_ms,buffer_size,buffer_size_tx;const char* user_agent;int(*crt_bundle_attach)(void*);int(*event_handler)(esp_http_client_event_t*);void* user_data;bool keep_alive_enable;int tls_dyn_buf_strategy;};
Http* esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_open(Http*,int);
int esp_http_client_fetch_headers(Http*);
int esp_http_client_get_status_code(Http*);
int esp_http_client_set_redirection(Http*);
int esp_http_client_set_url(Http*,const char*);
void esp_http_client_set_header(Http*,const char*,const char*);
void esp_http_client_set_timeout_ms(Http*,int);
void esp_http_client_close(Http*);
void esp_http_client_cleanup(Http*);
int esp_http_client_get_url(Http*,char*,size_t);
int esp_http_client_read(Http*,char*,size_t);
bool esp_http_client_is_complete_data_received(Http*);
''',
    'simple_dec/esp_audio_simple_dec.h': '#pragma once\nenum esp_audio_simple_dec_type_t {ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,ESP_AUDIO_SIMPLE_DEC_TYPE_TS};\n',
    'esp_crt_bundle.h': '#pragma once\ninline int esp_crt_bundle_attach(void*) {return 0;}\n',
    'esp_log.h': '#pragma once\n#include <cstdio>\n#define ESP_LOGW(tag,...) do {(void)tag; printf(__VA_ARGS__); puts("");} while(0)\n#define ESP_LOGI ESP_LOGW\n',
    'esp_timer.h': '#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n',
    'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(x) (x)\n',
    'freertos/task.h': '#pragma once\nvoid vTaskDelay(unsigned);\n',
}
TEST = r'''
#include "radio/radio_source.h"
#include "radio/radio_hls.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
struct Response {std::string body,mime="application/vnd.apple.mpegurl",redirect;int status=200;bool truncated=false;int waits=0;};
struct Http {esp_http_client_config_t cfg;std::string url;Response response;size_t offset=0;};
static std::map<std::string,std::vector<Response>> responses;
static std::map<std::string,unsigned> requests;
static unsigned live=0,peak=0,waits=0;
static int64_t clock_us=0;
static bool cancelled=false,cancel_wait=false,cancel_open=false,cancel_headers=false;
static unsigned header_calls=0;
Http* esp_http_client_init(const esp_http_client_config_t*c){assert(c->tls_dyn_buf_strategy==HTTP_TLS_DYN_BUF_RX_STATIC);auto*p=new Http; p->cfg=*c;p->url=c->url;++live;peak=std::max(peak,live);return p;}
int esp_http_client_open(Http*p,int){assert(p->cfg.timeout_ms>=6000);auto i=responses.find(p->url);if(i==responses.end())return -1;auto n=requests[p->url]++;p->response=i->second[std::min<size_t>(n,i->second.size()-1)];p->offset=0;if(cancel_open)cancelled=true;return 0;}
int esp_http_client_fetch_headers(Http*p){++header_calls;if(cancel_headers)cancelled=true;char key[]="Content-Type";esp_http_client_event_t e{HTTP_EVENT_ON_HEADER,p->cfg.user_data,key,p->response.mime.data()};p->cfg.event_handler(&e);if(!p->response.redirect.empty()){char location[]="Location";e.header_key=location;e.header_value=p->response.redirect.data();p->cfg.event_handler(&e);}return int(p->response.body.size());}
int esp_http_client_get_status_code(Http*p){return p->response.status;}
int esp_http_client_set_redirection(Http*p){char next[512];if(!radio_hls::resolve(p->url.c_str(),p->response.redirect.c_str(),next,sizeof(next)))return -1;p->url=next;return 0;}
int esp_http_client_set_url(Http*p,const char*url){assert(strstr(url,"://"));p->url=url;return 0;}
void esp_http_client_set_header(Http*,const char*,const char*){}
void esp_http_client_set_timeout_ms(Http*,int){}
void esp_http_client_close(Http*){}
void esp_http_client_cleanup(Http*p){assert(live);--live;delete p;}
// ESP-IDF 5.5.3's getter omits connection_info.query. Model that behavior so
// tests cannot accidentally bless losing CDN session/signature parameters.
int esp_http_client_get_url(Http*p,char*out,size_t size){auto clean=p->url.substr(0,p->url.find('?'));if(clean.size()>=size)return -1;strcpy(out,clean.c_str());return 0;}
int esp_http_client_read(Http*p,char*out,size_t size){if(p->response.waits>0){--p->response.waits;return -ESP_ERR_HTTP_EAGAIN;}auto n=std::min({size,p->response.body.size()-p->offset,size_t(73)});memcpy(out,p->response.body.data()+p->offset,n);p->offset+=n;return int(n);}
bool esp_http_client_is_complete_data_received(Http*p){return p->offset==p->response.body.size()&&!p->response.truncated;}
int64_t esp_timer_get_time(){return clock_us;}
void vTaskDelay(unsigned ms){clock_us+=ms*1000;++waits;if(cancel_wait)cancelled=true;}
bool stop(void*){return cancelled;}
std::string adts(char payload){std::string s(1000,payload);s[0]=char(0xff);s[1]=char(0xf1);return s;}
void reset(){assert(!live);responses.clear();requests.clear();clock_us=0;waits=0;cancelled=cancel_wait=cancel_open=cancel_headers=false;header_calls=0;}
std::string drain(RadioSource&s){std::string out;uint8_t data[127];for(unsigned i=0;i<1000;++i){int n=s.read(data,sizeof(data));if(n<=0)return out;out.append((char*)data,n);}assert(false);return {};}
int main(){
 reset();
 const std::string signed_url="https://cdn.test/live.m3u8?session=one&sig=a%2Bb";
 responses[signed_url]={
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:3,\na.aac?part=1\n"},
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:3,\na.aac?part=1\n#EXTINF:3,\nb.aac?part=2\n"},
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:11\n#EXTINF:3,\nb.aac?part=2\n#EXTINF:3,\nc.aac?part=3\n#EXT-X-ENDLIST\n"}};
 for(unsigned i=0;i<3;i++)responses["https://cdn.test/"+std::string(1,char('a'+i))+".aac?part="+std::to_string(i+1)]={{adts(char('a'+i)),"audio/aac"}};
 {RadioSource s(stop,nullptr);assert(s.open(signed_url.c_str()));assert(drain(s)==adts('a')+adts('b')+adts('c')&&s.complete());assert(requests[signed_url]==3);assert(!requests["https://cdn.test/live.m3u8"]);}
 reset();
 responses["http://r/start"]={{"","","https://cdn.test/live/index.m3u8?token=old",302}};
 responses["https://cdn.test/live/index.m3u8?token=old"]={{"","","?token=new&sig=a%2Bb",307}};
 const std::string redirected="https://cdn.test/live/index.m3u8?token=new&sig=a%2Bb";
 responses[redirected]={
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:20\n#EXTINF:3,\na.aac?part=1\n"},
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:20\n#EXTINF:3,\na.aac?part=1\n#EXTINF:3,\nb.aac?part=2\n#EXT-X-ENDLIST\n"}};
 responses["https://cdn.test/live/a.aac?part=1"]={{adts('a'),"audio/aac"}};responses["https://cdn.test/live/b.aac?part=2"]={{adts('b'),"audio/aac"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/start"));assert(drain(s)==adts('a')+adts('b')&&s.complete());assert(requests[redirected]==2);}
 reset();
 responses["http://r/master.m3u8"]={{"#EXTM3U\n#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=234000\nmedia.m3u8?session=live\n"}};
 responses["http://r/media.m3u8?session=live"]={
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:30\n#EXTINF:3,\na.aac\n"},
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:30\n#EXTINF:3,\na.aac\n#EXTINF:3,\nb.aac\n#EXT-X-ENDLIST\n"}};
 responses["http://r/a.aac"]={{adts('a'),"audio/aac"}};responses["http://r/b.aac"]={{adts('b'),"audio/aac"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/master.m3u8"));assert(drain(s)==adts('a')+adts('b')&&s.complete());assert(requests["http://r/media.m3u8?session=live"]==2);}
 for(const auto& target:{std::string("ftp://invalid/audio"),std::string("https://cdn.test/")+std::string(600,'x'),std::string("/audio\r\nX-Test: injected")}){
 reset();responses["http://r/redirect"]={{"","",target,302}};{RadioSource s(stop,nullptr);assert(!s.open("http://r/redirect"));}}
 reset();responses["http://r/mp3"]={{std::string("\xff\xfb")+std::string(2000,'m'),"audio/mpeg"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/mp3")&&s.type()==ESP_AUDIO_SIMPLE_DEC_TYPE_MP3);assert(drain(s)==responses["http://r/mp3"][0].body&&s.complete());}
 reset();responses["http://r/wrong.mp3"]={{adts('a'),"audio/aacp"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/wrong.mp3")&&s.type()==ESP_AUDIO_SIMPLE_DEC_TYPE_AAC);assert(drain(s)==adts('a'));}
 reset();
 responses["http://r/start.m3u8"]={{"","", "https://cdn.test/live/master.m3u8",302}};
 responses["https://cdn.test/live/master.m3u8"]={{"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\nmedia.m3u8\n"}};
 responses["https://cdn.test/live/media.m3u8"]={{"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:10\n#EXTINF:6,\na.aac\n#EXTINF:6,\nb.aac\n#EXT-X-ENDLIST\n"}};
 responses["https://cdn.test/live/a.aac"]={{adts('a'),"audio/aac"}};responses["https://cdn.test/live/b.aac"]={{adts('b'),"audio/aac"}};
 alignas(16) unsigned char workspace[RadioSource::kHlsWorkspaceBytes+32];memset(workspace,0x5a,sizeof(workspace));
 {RadioSource s(stop,nullptr,workspace+16,RadioSource::kHlsWorkspaceBytes);assert(s.open("http://r/start.m3u8"));assert(drain(s)==adts('a')+adts('b')&&s.complete());assert(waits==0);}
 for(unsigned i=0;i<16;++i){assert(workspace[i]==0x5a);assert(workspace[sizeof(workspace)-1-i]==0x5a);}
 assert(peak==1);
 reset();
 responses["http://r/live.m3u8"]={
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:20\n#EXTINF:6,\na.ts\n"},
 {"#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:20\n#EXTINF:6,\na.ts\n#EXTINF:6,\nb.ts\n#EXT-X-ENDLIST\n"}};
 std::string ts(1000,'t');ts[0]=ts[188]=ts[376]=0x47;
 responses["http://r/a.ts"]={{ts,"video/mp2t"}};responses["http://r/b.ts"]={{ts,"video/mp2t"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/live.m3u8")&&s.type()==ESP_AUDIO_SIMPLE_DEC_TYPE_TS);assert(drain(s)==ts+ts);assert(requests["http://r/a.ts"]==1&&requests["http://r/b.ts"]==1&&waits==0);}
 reset();responses["http://r/live.m3u8"]={{"#EXTM3U\n#EXTINF:6,\na.aac\n"}};responses["http://r/a.aac"]={{adts('a'),"audio/aac"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/live.m3u8"));cancel_wait=true;assert(drain(s)==adts('a'));assert(cancelled&&waits==1);}
 reset();responses["http://r/disc.m3u8"]={{"#EXTM3U\n#EXTINF:6,\na.aac\n#EXT-X-DISCONTINUITY\n#EXTINF:6,\nb.aac\n#EXT-X-ENDLIST\n"}};responses["http://r/a.aac"]={{adts('a'),"audio/aac"}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/disc.m3u8"));assert(drain(s)==adts('a'));assert(!requests["http://r/b.aac"]);}
 for(const auto& body:{std::string("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\"\n#EXTINF:6,\na.ts\n"),std::string("#EXTM3U\n")+std::string(2000,'x'),std::string("<html>error</html>")}){
 reset();responses["http://r/bad.m3u8"]={{body}};{RadioSource s(stop,nullptr);assert(!s.open("http://r/bad.m3u8"));}}
 reset();responses["http://r/trunc.m3u8"]={{"#EXTM3U\n#EXTINF:6,\na.ts\n","application/vnd.apple.mpegurl","",200,true}};
 {RadioSource s(stop,nullptr);assert(!s.open("http://r/trunc.m3u8"));}
 reset();responses["http://r/delayed"]={{std::string("\xff\xfb")+std::string(2000,'m'),"audio/mpeg","",200,false,3}};
 {RadioSource s(stop,nullptr);assert(s.open("http://r/delayed"));assert(drain(s)==responses["http://r/delayed"][0].body);}
 reset();responses["http://r/cancel"]={{adts('a'),"audio/aac"}};cancel_open=true;
 {RadioSource s(stop,nullptr);assert(!s.open("http://r/cancel")&&cancelled&&header_calls==0);}
 reset();responses["http://r/cancel"]={{adts('a'),"audio/aac"}};cancel_headers=true;
 {RadioSource s(stop,nullptr);assert(!s.open("http://r/cancel")&&cancelled&&header_calls==1);}
 assert(live==0);puts("Production HLS transport: redirects, segments, no replay, refresh, cancellation, cleanup, invalid bodies PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='radio-source-') as temp:
    tmp = Path(temp)
    for name, source in HEADERS.items():
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)
    (tmp / 'test.cc').write_text(TEST)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', '-fsanitize=address,undefined',
                    '-I'+str(tmp), '-I'+str(ROOT/'main/vibe'), str(tmp/'test.cc'),
                    str(ROOT/'main/vibe/radio/radio_source.cc'), '-o', str(tmp/'test')], check=True)
    subprocess.run([str(tmp/'test')], check=True)
