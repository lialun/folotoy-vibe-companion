"""Exercise the real transport observers without modifying SDK ownership."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
main=r'''
#include "ble_hci_probe.c"
#include <assert.h>
#include <string.h>
static int64_t micros;
static int tx_result,event_result,process_result=23;
static unsigned events,processes,process_delay;
static void *expected_event;
int64_t esp_timer_get_time(void){return micros;}
int __real_ble_transport_to_ll_acl_impl(struct os_mbuf *buffer){
 assert(buffer->alive);buffer->alive=0;buffer->length=0;return tx_result;
}
int __real_ble_transport_to_hs_evt_impl(void *buffer){assert(buffer==expected_event);++events;return event_result;}
int __real_ble_hs_hci_evt_process(struct ble_hci_ev *event){
 assert((void *)event==expected_event);++processes;micros+=process_delay;((uint8_t *)event)[0]=0;return process_result;
}
int main(void){
 micros=1000000;vibe_ble_probe_recording(true);
 struct os_mbuf packet={821,1};assert(!__wrap_ble_transport_to_ll_acl_impl(&packet));
 vibe_ble_radio_probe_t stats=vibe_ble_radio_probe();assert(stats.packets==1 && stats.bytes==821 && !packet.alive);
 packet=(struct os_mbuf){50,1};tx_result=7;assert(__wrap_ble_transport_to_ll_acl_impl(&packet)==7);
 stats=vibe_ble_radio_probe();assert(stats.packets==1 && stats.failures==1 && stats.bytes==821);
 uint8_t complete[]={0x13,5,1,1,0,2,0},copy[sizeof(complete)];memcpy(copy,complete,sizeof(copy));
 expected_event=complete;micros=1100000;assert(!__wrap_ble_transport_to_hs_evt_impl(complete));
 assert(!memcmp(copy,complete,sizeof(copy)) && events==1);
 stats=vibe_ble_radio_probe();assert(stats.completed==2 && stats.gap_ms==100 && stats.processed==0);
 micros=1250000;process_delay=20000;assert(__wrap_ble_hs_hci_evt_process((void *)complete)==23);
 stats=vibe_ble_radio_probe();assert(stats.processed==2 && stats.host_lag==150 && stats.process_ms==20 && processes==1);
 vibe_ble_probe_recording(false);
 uint8_t multiple[]={0x13,9,2,1,0,2,0,2,0,3,0};expected_event=multiple;micros=6000000;
 assert(!__wrap_ble_transport_to_hs_evt_impl(multiple));__wrap_ble_hs_hci_evt_process((void *)multiple);
 stats=vibe_ble_radio_probe();assert(stats.completed==7 && stats.processed==7 && stats.gap_ms==100 && stats.host_lag==150 && stats.process_ms==20);
 uint8_t malformed[]={0x13,5,2,1,0,9,0};expected_event=malformed;event_result=91;
 assert(__wrap_ble_transport_to_hs_evt_impl(malformed)==91 && vibe_ble_radio_probe().completed==7);
 event_result=0;process_delay=0;micros=7000000;vibe_ble_probe_recording(true);
 memcpy(complete,copy,sizeof(copy));expected_event=complete;__wrap_ble_transport_to_hs_evt_impl(complete);
 micros=7100000;__wrap_ble_transport_to_hs_evt_impl(complete); /* reused buffer after an unprocessed event */
 micros=7110000;__wrap_ble_hs_hci_evt_process((void *)complete);
 stats=vibe_ble_radio_probe();assert(stats.untracked==1 && stats.host_lag==10 && stats.process_ms==0);
 vibe_hci_probe_t direct={0};memcpy(complete,copy,sizeof(copy));
 vibe_hci_probe_record(&direct,true,UINT32_MAX-10);
 vibe_hci_probe_event(&direct,complete,sizeof(complete),5);
 assert(atomic_load(&direct.max_gap)==16);
 vibe_hci_probe_event(&direct,complete,2,8);assert(atomic_load(&direct.completed)==2);
 puts("HCI observer: ownership, return values, real completion vs submission, host lag, idle, malformed events and clock wrap PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='vibe-hci-probe-') as directory:
    temp=Path(directory)
    (temp/'os').mkdir()
    (temp/'os/os_mbuf.h').write_text('#pragma once\nstruct os_mbuf{unsigned length;int alive;};\n#define OS_MBUF_PKTLEN(p) ((p)->length)\n')
    (temp/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (temp/'main.c').write_text(main)
    executable=temp/'probe'
    subprocess.run([os.getenv('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(temp),'-I'+str(ROOT/'main/vibe'),str(temp/'main.c'),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
