"""Exercise the production bounded/cancellable HTTP transport without networking."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
'esp_http_client.h': r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_EAGAIN 1
#define HTTP_EVENT_ON_DATA 3
typedef struct { int event_id,data_len;void *data,*user_data; } esp_http_client_event_t;
typedef struct Http *esp_http_client_handle_t;
typedef struct { const char *url; int timeout_ms,buffer_size,buffer_size_tx;void *crt_bundle_attach;bool is_async,disable_auto_redirect;esp_err_t (*event_handler)(esp_http_client_event_t *);void *user_data; } esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
void esp_http_client_cleanup(esp_http_client_handle_t);
''',
'esp_crt_bundle.h':'#define esp_crt_bundle_attach ((void *)1)\n',
'freertos/FreeRTOS.h':'#define pdMS_TO_TICKS(x) (x)\n',
'freertos/task.h':'void vTaskDelay(unsigned);\n',
}
TEST = r'''
#include "weather_transport.h"
#include "esp_http_client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Http { int unused; } handle;
static esp_http_client_config_t cfg;
static unsigned calls,cleanups,waits;
static int mode;
static uint64_t now;
static bool permitted=true;
bool allowed(void){return permitted;}
uint64_t clock_ms(void){return now;}
void vTaskDelay(unsigned ms){now+=ms;++waits;if(mode==2)permitted=false;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *value){cfg=*value;assert(cfg.is_async && cfg.timeout_ms==1000 && cfg.disable_auto_redirect);return mode==5?NULL:&handle;}
esp_err_t esp_http_client_perform(esp_http_client_handle_t h){
 assert(h==&handle);++calls;
 if(mode==1 || mode==2)return ESP_ERR_HTTP_EAGAIN;
 if(mode==3)return ESP_FAIL;
 char data[4100];memset(data,'x',sizeof(data));
 if(mode==4){esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_DATA,.data_len=4100,.data=data,.user_data=cfg.user_data};cfg.event_handler(&e);return ESP_OK;}
 if(mode==8)return ESP_OK; /* Empty */
 for(unsigned i=0;i<2;++i){esp_http_client_event_t e={.event_id=HTTP_EVENT_ON_DATA,.data_len=2,.data=i?"}":"{x",.user_data=cfg.user_data};if(i)e.data_len=1;cfg.event_handler(&e);}
 return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return mode==6?500:200;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return mode!=7;}
void esp_http_client_cleanup(esp_http_client_handle_t h){assert(h==&handle);++cleanups;}
int main(void){
 for(mode=0;mode<=8;++mode){
 calls=cleanups=waits=0;now=0;permitted=true;
 char *body=vibe_weather_fetch("https://weather.test",allowed,clock_ms);
 if(mode==0){assert(body && !strcmp(body,"{x}"));free(body);}else assert(!body);
 assert(cleanups==(mode==5?0:1));
 if(mode==1)assert(now==15000 && calls==750);
 if(mode==2)assert(waits==1 && calls==1);
 }
 permitted=false;calls=cleanups=0;assert(!vibe_weather_fetch("https://weather.test",allowed,clock_ms));assert(!calls && !cleanups);
 puts("Production weather HTTP: complete body, cancellation, deadline, overflow, status/error and cleanup PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='weather-http-') as directory:
    temp=Path(directory)
    for name,source in HEADERS.items():
        path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(source)
    (temp/'test.c').write_text(TEST)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(temp),'-I'+str(ROOT/'main/vibe'),str(temp/'test.c'),str(ROOT/'main/vibe/weather_transport.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
