#include "trusted_computers.h"
#include <assert.h>
#include <stdio.h>

/* Exercise the production NVS adapter with transaction failures. */
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 1
#define NVS_READWRITE 1
#define ESP_LOGW(...) ((void)0)
static vibe_trusted_computers_t v_computers;
static bool v_legacy_computer;
static struct {char token[65];} v_status;
static uint8_t disk[VIBE_TRUSTED_BLOB_MAX],staged[VIBE_TRUSTED_BLOB_MAX];
static size_t disk_size,staged_size;
static unsigned failure;
static char legacy[65];
static int nvs_open(const char *name,int mode,nvs_handle_t *n) {(void)name;(void)mode;*n=1;return failure==1?-1:0;}
static int nvs_set_blob(nvs_handle_t n,const char *key,const void *bytes,size_t size) {
    (void)n;assert(!strcmp(key,"computers"));if (failure==2)return -1;
    memcpy(staged,bytes,size);staged_size=size;return 0;
}
static int nvs_commit(nvs_handle_t n) {
    (void)n;if (failure==3)return -1;
    memcpy(disk,staged,staged_size);disk_size=staged_size;return 0;
}
static void nvs_close(nvs_handle_t n) {(void)n;staged_size=0;}
static int nvs_get_blob(nvs_handle_t n,const char *key,void *bytes,size_t *size) {
    (void)n;assert(!strcmp(key,"computers"));if (!disk_size)return ESP_ERR_NVS_NOT_FOUND;
    if (*size<disk_size)return -1;
    memcpy(bytes,disk,disk_size);*size=disk_size;return 0;
}
static int nvs_get_str(nvs_handle_t n,const char *key,char *bytes,size_t *size) {
    (void)n;assert(!strcmp(key,"token"));if (!legacy[0])return ESP_ERR_NVS_NOT_FOUND;
    assert(*size>=65);memcpy(bytes,legacy,65);*size=65;return 0;
}
#include "trusted_store.inc"
int main(void) {
    const char *ids[]={"11111111-1111-1111-1111-111111111111","22222222-2222-2222-2222-222222222222",
        "33333333-3333-3333-3333-333333333333","44444444-4444-4444-4444-444444444444",
        "55555555-5555-5555-5555-555555555555"};
    char tokens[5][65];
    for (unsigned i=0;i<5;i++) {memset(tokens[i],'a'+(int)i,64);tokens[i][64]=0;}
    strcpy(legacy,tokens[0]);load_computers(1);
    assert(v_legacy_computer && !v_computers.count && !strcmp(v_status.token,tokens[0]));
    vibe_trusted_computers_t next={0};assert(vibe_trusted_put(&next,ids[0],tokens[0]));
    for (failure=1;failure<=3;failure++) {
        assert(!save_computers(&next));assert(v_legacy_computer && !v_computers.count && !disk_size);
    }
    failure=0;assert(save_computers(&next));assert(!v_legacy_computer && v_computers.count==1);
    load_computers(1);assert(!v_status.token[0] && vibe_trusted_matches(&v_computers,ids[0],tokens[0]));
    for (unsigned i=1;i<4;i++) assert(vibe_trusted_put(&next,ids[i],tokens[i]));
    assert(!vibe_trusted_put(&next,ids[4],tokens[4]) && next.count==4);
    assert(!vibe_trusted_put(&next,ids[4],tokens[0]));
    assert(!vibe_trusted_put(&next,"bad",tokens[4]) && !vibe_trusted_put(&next,ids[4],"bad"));
    assert(save_computers(&next));load_computers(1);assert(v_computers.count==4);
    for (unsigned i=0;i<4;i++) assert(vibe_trusted_matches(&v_computers,ids[i],tokens[i]));
    assert(vibe_trusted_remove(&next,ids[1]) && !vibe_trusted_remove(&next,ids[1]));
    assert(!vibe_trusted_has(&next,ids[1]) && vibe_trusted_has(&next,ids[2]));
    assert(save_computers(&next));load_computers(1);assert(v_computers.count==3);
    assert(vibe_trusted_put(&next,ids[0],tokens[4]));
    assert(!vibe_trusted_matches(&next,ids[0],tokens[0]) && vibe_trusted_matches(&next,ids[0],tokens[4]));
    uint8_t encoded[VIBE_TRUSTED_BLOB_MAX];size_t size=vibe_trusted_encode(&next,encoded,sizeof(encoded));assert(size);
    vibe_trusted_computers_t before=v_computers;
    assert(!vibe_trusted_decode(&v_computers,encoded,size-1) && !memcmp(&v_computers,&before,sizeof(before)));
    encoded[4]=5;assert(!vibe_trusted_decode(&v_computers,encoded,size));
    next=(vibe_trusted_computers_t){0};assert(save_computers(&next));load_computers(1);
    assert(!v_computers.count && !v_legacy_computer && !v_status.token[0]); /* Old token cannot resurrect. */
    disk[0]='X';load_computers(1);assert(!v_computers.count && !v_legacy_computer && !v_status.token[0]);
    for (unsigned i=0;i<100;i++) {
        next=(vibe_trusted_computers_t){0};assert(vibe_trusted_put(&next,ids[0],tokens[0]));
        assert(vibe_trusted_put(&next,ids[1],tokens[1]) && save_computers(&next));load_computers(1);
        assert(vibe_trusted_remove(&v_computers,ids[i%2]) && save_computers(&v_computers));load_computers(1);
        assert(v_computers.count==1 && vibe_trusted_has(&v_computers,ids[(i+1)%2]));
    }
    puts("Trusted computers: bounded codec, distinct credentials, persistence faults, legacy migration, revocation and 100 reboot cycles PASS");
}
