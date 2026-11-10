#include "wifi_profiles.h"
#include "wifi_retry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned writes;
static bool fail_save;
static unsigned char disk[VIBE_WIFI_BLOB_MAX];
static size_t disk_size;
static bool persist(const void *blob,size_t size,void *context) {
    assert(context==&writes);writes++;
    if (fail_save) return false;
    memcpy(disk,blob,size);disk_size=size;return true;
}
int main(void) {
    vibe_wifi_profiles_t current={0},next={0},decoded={0};
    vibe_wifi_credential_t home={.ssid="Home WiFi",.password="test-only-home"};
    vibe_wifi_credential_t office={.ssid="Office WiFi",.password="test-only-office"};
    assert(vibe_wifi_profiles_put(&next,&home));
    assert(vibe_wifi_profiles_commit(&current,&next,persist,&writes));
    assert(vibe_wifi_profiles_decode(&decoded,disk,disk_size));assert(!memcmp(&decoded,&current,sizeof(current)));
    /* Startup at either saved site reaches it even if the other was preferred. */
    assert(vibe_wifi_profiles_put(&next,&office));assert(next.count==2 && next.preferred==1);
    assert(vibe_wifi_profiles_commit(&current,&next,persist,&writes));
    for (unsigned visible=0;visible<2;visible++) {
        bool connected=false;
        for (unsigned attempt=0;attempt<current.count;attempt++)
            if (vibe_wifi_retry_index(current.count,current.preferred,attempt)==visible) connected=true;
        assert(connected);
    }
    assert(vibe_wifi_retry_index(2,1,0)==1 && vibe_wifi_retry_index(2,1,1)==0);
    vibe_wifi_retry_t retry;
    vibe_wifi_retry_reset(&retry,1000);
    assert(!vibe_wifi_retry_due(&retry,1000,0));
    assert(vibe_wifi_retry_due(&retry,1000,2));vibe_wifi_retry_started(&retry,1000);
    assert(!vibe_wifi_retry_due(&retry,2000,2));
    assert(!vibe_wifi_retry_timeout(&retry,15999));assert(vibe_wifi_retry_timeout(&retry,16000));
    vibe_wifi_retry_failed(&retry,16000,2);
    assert(retry.attempt==1 && !retry.round_done);
    assert(!vibe_wifi_retry_due(&retry,16499,2));assert(vibe_wifi_retry_due(&retry,16500,2));
    vibe_wifi_retry_started(&retry,16500);vibe_wifi_retry_failed(&retry,17000,2);
    assert(retry.attempt==0 && retry.round_done && retry.round==1 && retry.due==22000);
    assert(!vibe_wifi_retry_due(&retry,21999,2));assert(vibe_wifi_retry_due(&retry,22000,2));
    for (unsigned round=0;round<10;round++) {
        uint64_t now=retry.due;
        for (unsigned attempt=0;attempt<5;attempt++) {
            assert(retry.attempt==attempt);
            vibe_wifi_retry_started(&retry,now);vibe_wifi_retry_failed(&retry,now+1000,5);now=retry.due;
        }
        assert(retry.attempt==0 && retry.due-now<=60000);
    }
    assert(vibe_wifi_retry_delay_ms(0)==5000 && vibe_wifi_retry_delay_ms(1)==10000);
    assert(vibe_wifi_retry_delay_ms(2)==20000 && vibe_wifi_retry_delay_ms(3)==40000);
    assert(vibe_wifi_retry_delay_ms(4)==60000 && vibe_wifi_retry_delay_ms(100)==60000);
    // A completed failed round must not cancel the next site's association or
    // DHCP merely because twenty seconds have passed since boot.
    vibe_wifi_retry_reset(&retry,1000);
    assert(vibe_wifi_offer_setup(&retry,1000,1000,false,false));
    assert(!vibe_wifi_offer_setup(&retry,30000,1000,true,false));
    vibe_wifi_retry_started(&retry,1000);vibe_wifi_retry_failed(&retry,2000,1);
    assert(vibe_wifi_offer_setup(&retry,30000,1000,true,false));
    vibe_wifi_retry_started(&retry,30000);
    assert(!vibe_wifi_offer_setup(&retry,30001,1000,true,false));
    retry.pending=false;
    assert(!vibe_wifi_offer_setup(&retry,30001,1000,true,true));
    assert(!vibe_wifi_disconnect_relevant(office.ssid,(const uint8_t *)home.ssid,strlen(home.ssid),false));
    assert(!vibe_wifi_disconnect_relevant(office.ssid,(const uint8_t *)office.ssid,strlen(office.ssid),true));
    assert(vibe_wifi_disconnect_relevant(office.ssid,(const uint8_t *)office.ssid,strlen(office.ssid),false));
    uint8_t full_ssid[32];memset(full_ssid,'s',sizeof(full_ssid));
    char target[33];memset(target,'s',32);target[32]=0;
    assert(vibe_wifi_disconnect_relevant(target,full_ssid,32,false));
    assert(!vibe_wifi_disconnect_relevant(target,full_ssid,33,false));
    assert(vibe_wifi_disconnect_relevant(target,NULL,0,false));
    unsigned before=writes;
    assert(vibe_wifi_profiles_commit(&current,&next,persist,&writes) && writes==before);
    strcpy(home.password,"test-only-updated");assert(vibe_wifi_profiles_put(&next,&home));
    assert(next.count==2 && next.preferred==0);
    fail_save=true;vibe_wifi_profiles_t previous=current;
    assert(!vibe_wifi_profiles_commit(&current,&next,persist,&writes));assert(!memcmp(&current,&previous,sizeof(current)));
    fail_save=false;assert(vibe_wifi_profiles_commit(&current,&next,persist,&writes));
    assert(!strcmp(current.entries[0].password,home.password));
    assert(vibe_wifi_profiles_put(&next,&next.entries[0])); /* Aliased edit. */
    for (unsigned i=2;i<VIBE_WIFI_PROFILE_MAX;i++) {
        vibe_wifi_credential_t extra={.password=""};snprintf(extra.ssid,sizeof(extra.ssid),"Open %u",i);
        assert(vibe_wifi_profiles_put(&next,&extra));
    }
    vibe_wifi_credential_t overflow={.ssid="Overflow"};previous=next;
    assert(!vibe_wifi_profiles_put(&next,&overflow));assert(!memcmp(&previous,&next,sizeof(next)));
    assert(vibe_wifi_profiles_remove(&next,"Office WiFi"));assert(next.count==4 && next.preferred==3);
    assert(vibe_wifi_profiles_remove(&next,"Open 4"));assert(next.preferred==0);
    unsigned char blob[VIBE_WIFI_BLOB_MAX];size_t size=vibe_wifi_profiles_encode(&next,blob,sizeof(blob));assert(size);
    assert(vibe_wifi_profiles_decode(&decoded,blob,size));assert(!memcmp(&decoded,&next,sizeof(next)));
    previous=decoded;
    for (size_t truncated=0;truncated<size;truncated++) {
        assert(!vibe_wifi_profiles_decode(&decoded,blob,truncated));assert(!memcmp(&decoded,&previous,sizeof(decoded)));
    }
    blob[5]=20;assert(!vibe_wifi_profiles_decode(&decoded,blob,size));blob[5]=next.preferred;
    blob[8]=0;assert(!vibe_wifi_profiles_decode(&decoded,blob,size));
    vibe_wifi_credential_t maximum={0};memset(maximum.ssid,'s',32);memset(maximum.password,'p',63);
    next=(vibe_wifi_profiles_t){0};assert(vibe_wifi_profiles_put(&next,&maximum));
    size=vibe_wifi_profiles_encode(&next,blob,sizeof(blob));assert(size==103);
    assert(vibe_wifi_profiles_decode(&decoded,blob,size));assert(!memcmp(&decoded,&next,sizeof(next)));
    assert(!vibe_wifi_profiles_encode(&next,blob,size-1));
    strcpy(maximum.password,"short");assert(!vibe_wifi_credential_valid(&maximum));
    printf("Wi-Fi profiles: compact storage, retained credentials, same-name update, rollback, bounds and automatic site selection PASS (%zu B bank)\n",sizeof(current));
}
