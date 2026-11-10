#include "radio/library.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail_allocate;
static void *station_calloc(size_t count,size_t bytes);
#define calloc station_calloc
#include "../../main/vibe/radio/library.c"
#undef calloc
static void *station_calloc(size_t count,size_t bytes) { return fail_allocate ? NULL : calloc(count,bytes); }
static unsigned writes;static bool fail;
static bool save(const vibe_station_list_t *list,void *context) { (void)context;assert(vibe_station_list_valid(list));++writes;return !fail; }
static cJSON *request(vibe_library_t *l,const char *json,uint64_t now,bool ok) {
    cJSON *req=cJSON_Parse(json);assert(req);cJSON *reply=vibe_library_handle(l,req,now,save,NULL);cJSON_Delete(req);
    assert(reply && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply,"ok"))==ok);return reply;
}
#define DO(json,now,ok) cJSON_Delete(request(&l,json,now,ok))
int main(void) {
    vibe_library_t l={.active=vibe_station_list_create(1)};
    assert(l.active);strcpy(l.active->stations[0].name,"原台");strcpy(l.active->stations[0].url,"http://example.org/old");
    assert(vibe_station_list_valid(l.active));
    DO("{\"type\":\"radio.begin\",\"transaction\":\"abc\",\"revision\":1,\"count\":2}",0,true);
    DO("{\"type\":\"radio.put\",\"transaction\":\"abc\",\"index\":1,\"station\":{\"name\":\"第二台\",\"url\":\"https://example.org/b\"}}",1,true);
    DO("{\"type\":\"radio.commit\",\"transaction\":\"abc\"}",2,false);
    assert(writes==0 && l.active->count==1); /* Partial/reordered datagrams never mutate the live list. */
    DO("{\"type\":\"radio.begin\",\"transaction\":\"abc\",\"revision\":1,\"count\":2}",3,true);
    assert(l.received==2); /* Repeated begin must not discard existing chunks. */
    DO("{\"type\":\"radio.put\",\"transaction\":\"abc\",\"index\":0,\"station\":{\"name\":\"第一台\",\"url\":\"https://example.org/a\"}}",4,true);
    fail=true;DO("{\"type\":\"radio.commit\",\"transaction\":\"abc\"}",5,false);assert(l.active->count==1);
    fail=false;DO("{\"type\":\"radio.commit\",\"transaction\":\"abc\"}",6,true);
    assert(writes==2 && l.active->revision==2 && l.active->count==2 && !strcmp(l.active->stations[0].name,"第一台"));
    DO("{\"type\":\"radio.commit\",\"transaction\":\"abc\"}",7,true);assert(writes==2); /* Lost reply retry. */
    DO("{\"type\":\"radio.get\",\"index\":1,\"revision\":1}",8,false);
    cJSON *reply=request(&l,"{\"type\":\"radio.get\",\"index\":1,\"revision\":2}",9,true);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(reply,"station"),"name")->valuestring,"第二台"));cJSON_Delete(reply);
    DO("{\"type\":\"radio.begin\",\"transaction\":\"def\",\"revision\":2,\"count\":21}",10,false);
    DO("{\"type\":\"radio.begin\",\"transaction\":\"def\",\"revision\":2,\"count\":1}",10,true);
    DO("{\"type\":\"radio.put\",\"transaction\":\"def\",\"index\":0,\"station\":{\"name\":\"坏台\",\"url\":\"file:///tmp/test\"}}",11,false);
    vibe_library_expire(&l,60010);assert(!l.staged && l.active->count==2);
    DO("{\"type\":\"radio.commit\",\"transaction\":\"def\"}",60011,false);
    vibe_station_list_t *bad=vibe_station_list_decode(l.active,vibe_station_list_bytes(l.active->count));assert(bad);
    bad->stations[1]=bad->stations[0];assert(!vibe_station_list_valid(bad));
    memset(bad->stations[1].name,'x',sizeof(bad->stations[1].name));assert(!vibe_station_list_valid(bad));free(bad);
    DO("{\"type\":\"radio.begin\",\"transaction\":\"eee\",\"revision\":2,\"count\":0}",70000,true);
    DO("{\"type\":\"radio.put\",\"transaction\":\"eee\",\"index\":0,\"station\":{\"name\":\"Bad\",\"url\":\"http://x/a\"}}",70001,false);
    DO("{\"type\":\"radio.commit\",\"transaction\":\"eee\"}",70002,true);
    assert(l.active->count==0 && l.active->revision==3);
    DO("{\"type\":\"radio.get\",\"index\":0}",70003,true);
    unsigned previous=writes;
    DO("{\"type\":\"radio.begin\",\"transaction\":\"fff\",\"revision\":3,\"count\":0}",70004,true);
    DO("{\"type\":\"radio.commit\",\"transaction\":\"fff\"}",70005,true);assert(writes==previous && l.active->revision==3);
    DO("{\"type\":\"radio.begin\",\"transaction\":\"aaa\",\"revision\":3,\"count\":1}",70006,true);
    DO("{\"type\":\"radio.abort\",\"transaction\":\"aaa\"}",70007,true);assert(!l.staged);
    const char *urls[]={"file:///tmp/a","http://","http:///x","https://?x","http://user:pass@host/a","http://host/a b","http://host/\\a"};
    for (unsigned i=0;i<sizeof(urls)/sizeof(*urls);i++) { vibe_station_t s={.name="test"};snprintf(s.url,sizeof(s.url),"%s",urls[i]);assert(!vibe_station_valid(&s)); }
    assert(vibe_station_list_bytes(20)==6132 && vibe_station_list_bytes(0)==12 && !vibe_station_list_create(21));
    DO("{\"type\":\"radio.begin\",\"transaction\":\"bbb\",\"revision\":3,\"count\":20}",80000,true);
    char item[512];
    for (int i=19;i>=0;i--) {
        if (i==17) continue;
        snprintf(item,sizeof(item),"{\"type\":\"radio.put\",\"transaction\":\"bbb\",\"index\":%d,\"station\":{\"name\":\"Station %d\",\"url\":\"https://example.org/%d\"}}",i,i,i);
        DO(item,80001,true);
    }
    assert(l.received&(UINT64_C(1)<<19));
    DO("{\"type\":\"radio.commit\",\"transaction\":\"bbb\"}",80002,false);
    assert(!l.active->count);
    DO("{\"type\":\"radio.put\",\"transaction\":\"bbb\",\"index\":17,\"station\":{\"name\":\"Station 17\",\"url\":\"https://example.org/17\"}}",80003,true);
    DO("{\"type\":\"radio.commit\",\"transaction\":\"bbb\"}",80004,true);
    assert(l.active->count==20 && l.active->revision==4 && !l.staged);
    cJSON *last=request(&l,"{\"type\":\"radio.get\",\"index\":19,\"revision\":4}",80005,true);
    assert(cJSON_GetObjectItemCaseSensitive(last,"capacity")->valueint==20);cJSON_Delete(last);
    vibe_station_list_t *decoded=vibe_station_list_decode(l.active,vibe_station_list_bytes(20));
    assert(decoded && decoded->count==20 && !strcmp(decoded->stations[19].name,"Station 19"));free(decoded);
    assert(!vibe_station_list_decode(l.active,vibe_station_list_bytes(20)-1));
    unsigned char packed[6132];size_t packed_size=vibe_station_list_storage_bytes(l.active);
    assert(packed_size<2048 && vibe_station_list_encode(l.active,packed,packed_size));
    decoded=vibe_station_list_decode(packed,packed_size);
    assert(decoded && decoded->revision==4 && decoded->count==20 && !memcmp(decoded->stations,l.active->stations,20*sizeof(vibe_station_t)));free(decoded);
    assert(!vibe_station_list_decode(packed,packed_size-1));
    packed[packed_size]=0;assert(!vibe_station_list_decode(packed,packed_size+1));
    assert(!vibe_station_list_encode(l.active,packed,packed_size-1));
    memset(packed+12,'x',49);assert(!vibe_station_list_decode(packed,packed_size));
    vibe_station_list_t *longest=vibe_station_list_create(20);assert(longest);
    for (unsigned i=0;i<20;i++) {
        memset(longest->stations[i].name,'N',48);
        memset(longest->stations[i].url,'a',256);
        memcpy(longest->stations[i].url,"http://host/",12);
        longest->stations[i].url[12]='A'+i/26;longest->stations[i].url[13]='A'+i%26;
    }
    assert(vibe_station_list_storage_bytes(longest)==6132);
    assert(vibe_station_list_encode(longest,packed,sizeof(packed)));
    decoded=vibe_station_list_decode(packed,sizeof(packed));assert(decoded && vibe_station_list_valid(decoded));free(decoded);free(longest);

    unsigned char old[12+16*sizeof(vibe_station_t)]={0};
    vibe_station_list_t *two=vibe_station_list_create(2);assert(two);
    two->revision=9;two->stations[0]=l.active->stations[0];two->stations[1]=l.active->stations[1];
    memcpy(old,two,vibe_station_list_bytes(2));free(two);
    decoded=vibe_station_list_decode(old,sizeof(old));assert(decoded && decoded->count==2 && decoded->revision==9);free(decoded);
    assert(!vibe_station_list_decode(old,sizeof(old)-1));
    DO("{\"type\":\"radio.begin\",\"transaction\":\"ccc\",\"revision\":4,\"count\":20}",90000,true);
    vibe_library_abort(&l);assert(!l.staged && l.active->count==20);
    vibe_station_list_t *before=l.active;
    fail_allocate=true;
    DO("{\"type\":\"radio.begin\",\"transaction\":\"ddd\",\"revision\":4,\"count\":20}",90001,false);
    assert(l.active==before && !l.staged && l.active->count==20);
    assert(!vibe_station_list_decode(l.active,vibe_station_list_bytes(20)));
    fail_allocate=false;
    vibe_library_clear(&l);assert(!l.active && !l.staged);
    puts("Radio list: 20 entries, 64-bit receipt mask, compact blobs/16-entry migration, atomic commit, rollback, revisions and expiry PASS");
}
