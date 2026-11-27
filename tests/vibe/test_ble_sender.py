"""Run the production notification sender with deterministic SDK/RTOS stubs."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sender=(ROOT/'main/vibe/ble_sender.inc').read_text()
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
#define OS_MBUF_TRAILINGSPACE(b) ((b)->om_omp->omp_databuf_len-(unsigned)((b)->om_data-(b)->om_databuf))
#define ESP_LOGI(tag,...) do{(void)(tag);assert(!tx_locked);++reports;if(0)printf(__VA_ARGS__);}while(0)
static unsigned clock_ms,contention,requested,notifications,allocations;
static unsigned trailing=256,appended,assembled;
static uint8_t saved[247],received[1537];
static bool pressure,tx_locked,wide_enabled,wide_pressure,small_live,wide_live,small_empty;
static unsigned reports,mtu=247,notify_attempts,fail_notify_at,controls;
static unsigned small_base,wide_base,small_held,wide_held;
static bool retain_notifications;
static bool ble_enabled=true,ready=true;
static void *tx_mutex=(void*)1,*links_mutex=(void*)2;
static uint8_t message_id;
static uint16_t tx_handle=20;
typedef struct{bool subscribed;uint32_t generation;}ble_link_t;
static ble_link_t link={true,1};
struct os_mempool{unsigned mp_num_free;};
struct os_mbuf_pool{struct os_mempool *omp_pool;unsigned omp_databuf_len;};
struct os_mbuf_pkthdr{unsigned short len,flags;};
struct os_mbuf{struct os_mbuf_pool *om_omp;uint8_t *om_data;unsigned char om_pkthdr_len;uint8_t om_databuf[400];};
static struct os_mempool pool={20},wide_pool={12};
static struct os_mbuf_pool mp={&pool,300},wide_mp={&wide_pool,380};
static struct os_mbuf mb,wide_mb;
static void initialize(struct os_mbuf *b,struct os_mbuf_pool *p,unsigned user,unsigned leading){
 b->om_omp=p;b->om_pkthdr_len=sizeof(struct os_mbuf_pkthdr)+user;
 b->om_data=b->om_databuf+b->om_pkthdr_len+leading;
}
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
static unsigned ble_att_mtu(uint16_t h){assert(h==1);return mtu;}
static struct os_mbuf *ble_hs_mbuf_att_pkt(void){
 if(small_empty)return NULL;
 assert(!small_live);small_live=true;++allocations;pool.mp_num_free=small_base?small_base-small_held:pressure?2:20;
 initialize(&mb,&mp,2,13);mp.omp_databuf_len=trailing+mb.om_pkthdr_len+13;return &mb;
}
static struct os_mbuf *os_msys_get_pkthdr(uint16_t size,uint16_t user){
 assert(size>mp.omp_databuf_len && user==2);
 if(!wide_enabled)return NULL;
 assert(!wide_live);wide_live=true;wide_pool.mp_num_free=wide_base?wide_base-wide_held:wide_pressure?2:12;
 initialize(&wide_mb,&wide_mp,user,0);return &wide_mb;
}
static unsigned os_msys_num_free(void){
 if(retain_notifications && small_base && wide_base)
  return small_base+1-small_held-(small_live?1:0)+wide_base+1-wide_held-(wide_live?1:0);
 return (pressure?15:24)-(wide_live?1:0);
}
static int os_mbuf_append(struct os_mbuf*b,const void*d,size_t n){assert(b&&d&&n<=244&&n<=OS_MBUF_TRAILINGSPACE(b));memcpy(saved,d,n);appended=n;return 0;}
static void os_mbuf_free_chain(struct os_mbuf*b){
 if(b==&mb){assert(small_live);small_live=false;}
 else{assert(b==&wide_mb && wide_live);wide_live=false;}
}
static int ble_gatts_notify_custom(uint16_t h,uint16_t a,struct os_mbuf*b){
 assert(h==1&&a==20&&(b==&mb||b==&wide_mb)&&appended>7);
 ++notify_attempts;
 if(fail_notify_at==notify_attempts){os_mbuf_free_chain(b);return BLE_HS_ENOMEM;}
 if((saved[2]&1) && saved[7]==2){++controls;os_mbuf_free_chain(b);return 0;}
 unsigned offset=saved[5]|saved[6]<<8;
 if(saved[2]&1)assembled=0;
 assert(offset==assembled);memcpy(received+offset,saved+7,appended-7);assembled+=appended-7;
 if(saved[2]&2)assert(assembled==(unsigned)(saved[3]|saved[4]<<8));
 assert((unsigned)(b->om_data-b->om_databuf)==b->om_pkthdr_len+13u);
 ++notifications;
 if(retain_notifications){if(b==&mb)++small_held;else ++wide_held;}
 os_mbuf_free_chain(b);return 0;
}
static void vTaskDelay(unsigned n){clock_ms+=n;}
'''
main=r'''
int main(void){
 ble_enabled=false;assert(vibe_ble_send_to(1,1,"x",1)<0 && !allocations && !tx_locked);
 ble_enabled=true;ready=false;assert(vibe_ble_send_to(1,1,"x",1)<0 && !allocations && !tx_locked);ready=true;
 uint8_t body[816]={0};
 contention=20;assert(!vibe_ble_send_to(1,6,body,sizeof(body)));assert(requested==30&&clock_ms==20&&notifications==4&&!tx_locked);
 trailing=211;notifications=0;assert(!vibe_ble_send_to(1,6,body,sizeof(body)));
 assert(notifications==5&&assembled==817&&received[0]==6&&!memcmp(received+1,body,sizeof(body)));
 trailing=256;
 clock_ms=0;contention=40;notifications=0;assert(vibe_ble_send_to(1,6,body,sizeof(body))==-1);assert(requested==30&&clock_ms==30&&!notifications&&!tx_locked);
 clock_ms=0;contention=20;pressure=true;assert(vibe_ble_send_to(1,6,body,sizeof(body))==-1);assert(clock_ms==30&&!tx_locked); /* ten remaining ms, not another thirty */
 clock_ms=0;contention=40;pressure=false;assert(!vibe_ble_send_to(1,2,body,20));assert(requested==50&&clock_ms==40&&!tx_locked);
 clock_ms=0;contention=0;pressure=true;assert(vibe_ble_send_to(1,1,body,sizeof(body))==-1);assert(clock_ms==30&&!tx_locked);
 clock_ms=0;pressure=true;wide_enabled=true;wide_pressure=false;contention=0;trailing=211;notifications=0;
 assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,0));
 assert(notifications==4 && assembled==817 && !memcmp(received+1,body,sizeof(body)) && clock_ms==0 && requested==0);
 assert(!small_live && !wide_live); /* Other-pool capacity works without consuming the control reserve. */
 small_empty=true;notifications=0;
 assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,0) && notifications==4 && !small_live && !wide_live);
 small_empty=false;
 for(unsigned limit=23;limit<=247;limit+=224){
  mtu=limit;notifications=0;
  assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,0) && assembled==817 && !small_live && !wide_live);
 }
 mtu=247;
 wide_pressure=true;clock_ms=0;notifications=0;
 assert(vibe_ble_voice_submit(1,6,body,sizeof(body),1,0)==-1 && !notifications && clock_ms==0 && !small_live && !wide_live);
 pressure=false;wide_pressure=false;wide_enabled=false;contention=20;clock_ms=0;
 assert(vibe_ble_voice_submit(1,6,body,sizeof(body),1,0)==-1 && clock_ms==0 && requested==0 && !tx_locked);
 assert(!reports);vibe_ble_voice_report();assert(reports==1 && !tx_locked);
 pressure=false;small_empty=false;wide_pressure=false;wide_enabled=true;contention=0;trailing=211;
 small_base=6;wide_base=6;retain_notifications=true;notifications=0;
 assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,0) && notifications==4);
 assert(small_held && wide_held && small_base+1-small_held>=4 && wide_base+1-wide_held>=4);
 assert(os_msys_num_free()>=6 && !small_live && !wide_live);
 unsigned prior=notifications;
 assert(vibe_ble_voice_submit(1,6,body,sizeof(body),1,1)==-1 && notifications==prior);
 /* Controller completion releases capacity; no extra heap or reduced receive
  * reserve is needed to resume the next complete frame. */
 small_held=wide_held=0;
 assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,1) && notifications==prior+4);
 small_held=wide_held=0;small_base=6;wide_base=9;notifications=0;
 for(unsigned sequence=0;sequence<2;sequence++) {
  assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,sequence));
  assert(small_base+1-small_held>=4 && wide_base+1-wide_held>=4 && os_msys_num_free()>=6);
 }
 prior=notifications;
 assert(vibe_ble_voice_submit(1,6,body,sizeof(body),1,2)==-1 && notifications==prior);
 small_held=wide_held=0;
 assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),1,2));
 pressure=false;small_empty=false;wide_pressure=false;wide_enabled=false;contention=0;
 retain_notifications=false;small_base=0;wide_base=0;small_held=wide_held=0;trailing=256;
 notifications=0;clock_ms=1000;fail_notify_at=notify_attempts+3;
 assert(vibe_ble_voice_submit(1,6,body,sizeof(body),88,0)>0);
 assert(notifications==2 && voice_cursor.active && voice_cursor.offset==2*(mtu-3-VIBE_BLE_HEADER));
 uint8_t prefix_id=voice_cursor.message;
 pressure=true;
 assert(vibe_ble_voice_submit(1,6,body,sizeof(body),88,0)==-1 && notifications==2);
 pressure=false; /* An existing prefix alone is not new credit progress. */
 assert(!vibe_ble_send_to(1,2,body,20));
 clock_ms+=360; /* First submission can outlive a redundant copy's deadline. */
 assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),88,0));
 assert(notifications==4 && voice_cursor.message==prefix_id && !voice_cursor.active && assembled==817);
 assert(!memcmp(received+1,body,sizeof(body))); /* No repeated prefix or mixed payload. */
 for(unsigned reason=0;reason<4;reason++) {
  notifications=0;clock_ms+=20;uint64_t session=100+reason;
  fail_notify_at=notify_attempts+3;
  assert(vibe_ble_voice_submit(1,6,body,sizeof(body),session,0)>0 && notifications==2);
  prefix_id=voice_cursor.message;
  if(reason==0)++session;
  if(reason==1)++link.generation;
  if(reason==2)clock_ms+=V_VOICE_RETAIN_MS+1;
  assert(!vibe_ble_voice_submit(1,6,body,sizeof(body),session,reason==3?1:0));
  assert(notifications==6 && voice_cursor.message!=prefix_id && assembled==817);
 }
 puts("Production BLE sender: resumable credit pressure, nonce/link/age isolation, ATT headroom, pool bounds and control interleave PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='vibe-ble-sender-') as directory:
 p=Path(directory);(p/'sender.c').write_text(preamble+sender+main)
 subprocess.run([os.getenv('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'main/vibe'),str(p/'sender.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
