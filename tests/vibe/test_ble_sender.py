"""Run the production notification sender with deterministic SDK/RTOS stubs."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'main/vibe/ble.c').read_text().split('int vibe_ble_send_to(',1)[1]
sender='int vibe_ble_send_to('+source
preamble=r'''
#include "ble_tx_budget.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define VIBE_BLE_PACKET_MAX 1537
#define VIBE_BLE_HEADER 7
#define BLE_HS_ENOMEM 6
#define BLE_HS_EAGAIN 1
#define MALLOC_CAP_8BIT 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(n) (n)
#define OS_ENTER_CRITICAL(n) ((n)=0)
#define OS_EXIT_CRITICAL(n) ((void)(n))
#define OS_MBUF_TRAILINGSPACE(b) ((void)(b),trailing)
#define ESP_LOGW(tag,...) do{(void)(tag);if(0)printf(__VA_ARGS__);}while(0)
static unsigned clock_ms,contention,requested,notifications,allocations;
static unsigned trailing=256,appended,assembled;
static uint8_t saved[247],received[1537];
static bool pressure,tx_locked;
static void *tx_mutex=(void*)1,*links_mutex=(void*)2;
static uint8_t message_id;
static uint16_t tx_handle=20;
typedef struct{bool subscribed;}ble_link_t;
static ble_link_t link={true};
struct os_mempool{unsigned mp_num_free;};
struct os_mbuf_pool{struct os_mempool *omp_pool;unsigned omp_databuf_len;};
struct os_mbuf{struct os_mbuf_pool *om_omp;};
static struct os_mempool pool={20};static struct os_mbuf_pool mp={&pool,256};static struct os_mbuf mb={&mp};
static uint64_t vibe_now(void){return clock_ms;}
static int xSemaphoreTake(void *s,unsigned timeout){
 if(s==links_mutex)return pdTRUE;
 assert(s==tx_mutex && !tx_locked);requested=timeout;
 unsigned waited=contention<timeout?contention:timeout;clock_ms+=waited;
 if(contention>timeout)return 0;
 tx_locked=true;return pdTRUE;
}
static void xSemaphoreGive(void*s){if(s==tx_mutex){assert(tx_locked);tx_locked=false;}}
static ble_link_t *find_link(uint16_t h){assert(h==1);return &link;}
static bool secure(uint16_t h){assert(h==1);return true;}
static unsigned ble_att_mtu(uint16_t h){assert(h==1);return 247;}
static struct os_mbuf *ble_hs_mbuf_att_pkt(void){++allocations;pool.mp_num_free=pressure?2:20;return &mb;}
static unsigned os_msys_num_free(void){return 20;}
static int os_mbuf_append(struct os_mbuf*b,const void*d,size_t n){assert(b&&d&&n<=244&&n<=trailing);memcpy(saved,d,n);appended=n;return 0;}
static void os_mbuf_free_chain(struct os_mbuf*b){assert(b==&mb);}
static int ble_gatts_notify_custom(uint16_t h,uint16_t a,struct os_mbuf*b){
 assert(h==1&&a==20&&b==&mb&&appended>7);unsigned offset=saved[5]|saved[6]<<8;
 if(saved[2]&1)assembled=0;
 assert(offset==assembled);memcpy(received+offset,saved+7,appended-7);assembled+=appended-7;
 if(saved[2]&2)assert(assembled==(unsigned)(saved[3]|saved[4]<<8));
 ++notifications;return 0;
}
static void vTaskDelay(unsigned n){clock_ms+=n;}
static unsigned heap_caps_get_free_size(unsigned n){(void)n;return 20000;}
static unsigned heap_caps_get_largest_free_block(unsigned n){(void)n;return 10000;}
'''
main=r'''
int main(void){
 uint8_t body[816]={0};
 contention=20;assert(!vibe_ble_send_to(1,6,body,sizeof(body)));assert(requested==30&&clock_ms==20&&notifications==4&&!tx_locked);
 trailing=211;notifications=0;assert(!vibe_ble_send_to(1,6,body,sizeof(body)));
 assert(notifications==5&&assembled==817&&received[0]==6&&!memcmp(received+1,body,sizeof(body)));
 trailing=256;
 clock_ms=0;contention=40;notifications=0;assert(vibe_ble_send_to(1,6,body,sizeof(body))==-1);assert(requested==30&&clock_ms==30&&!notifications&&!tx_locked);
 clock_ms=0;contention=20;pressure=true;assert(vibe_ble_send_to(1,6,body,sizeof(body))==-1);assert(clock_ms==30&&!tx_locked); /* ten remaining ms, not another thirty */
 clock_ms=0;contention=40;pressure=false;assert(!vibe_ble_send_to(1,2,body,20));assert(requested==50&&clock_ms==40&&!tx_locked);
 clock_ms=0;contention=0;pressure=true;assert(vibe_ble_send_to(1,1,body,sizeof(body))==-1);assert(clock_ms==30&&!tx_locked);
 puts("Production BLE sender: contended lock preserves complete frame, whole-call budget includes lock wait, exhausted budget and control headroom PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='vibe-ble-sender-') as directory:
 p=Path(directory);(p/'sender.c').write_text(preamble+sender+main)
 subprocess.run([os.getenv('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main/vibe'),str(p/'sender.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
