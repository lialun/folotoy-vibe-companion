/* Controller-only. One NVS blob is authoritative, including an empty list;
 * leaving the old token key cannot resurrect a removed computer on reboot. */
static bool save_computers(const vibe_trusted_computers_t *next) {
    static uint8_t encoded[VIBE_TRUSTED_BLOB_MAX];
    size_t size=vibe_trusted_encode(next,encoded,sizeof(encoded));
    nvs_handle_t n;
    if (!size || nvs_open("vibe",NVS_READWRITE,&n)!=ESP_OK) {memset(encoded,0,sizeof(encoded));return false;}
    bool ok=nvs_set_blob(n,"computers",encoded,size)==ESP_OK && nvs_commit(n)==ESP_OK;
    nvs_close(n);memset(encoded,0,sizeof(encoded));
    if (ok) {v_computers=*next;v_legacy_computer=false;}
    return ok;
}
static void load_computers(nvs_handle_t n) {
    v_computers=(vibe_trusted_computers_t){0};v_legacy_computer=false;v_status.token[0]=0;
    size_t z;
    uint8_t computers[VIBE_TRUSTED_BLOB_MAX];z=sizeof(computers);
    esp_err_t trust=nvs_get_blob(n,"computers",computers,&z);
    if (trust==ESP_OK) {
        if (!vibe_trusted_decode(&v_computers,computers,z)) ESP_LOGW(TAG,"Invalid computer registry; confirmation required");
    } else if (trust==ESP_ERR_NVS_NOT_FOUND) {
        z=sizeof(v_status.token);nvs_get_str(n,"token",v_status.token,&z);
        v_status.token[sizeof(v_status.token)-1]=0;
        v_legacy_computer=vibe_connect_digest(v_status.token);
        if (!v_legacy_computer) v_status.token[0]=0;
    }
    memset(computers,0,sizeof(computers));
}
