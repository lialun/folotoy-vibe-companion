/* Revoke the application session without erasing OS-level BLE encryption
 * bonds. A retained bond never bypasses token/physical approval checks. */
static void finish_release(void) {
    struct sockaddr_in old_wifi={0},old_ble={0};bool had_ble=false;char old_client[37]={0};
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool released=release_after_send && release_generation==owner_generation;
    if (released) {
        old_wifi=peer;old_ble=ble_peer;had_ble=ble_owner;
        strlcpy(old_client,owner_client,sizeof(old_client));
        memset(&peer,0,sizeof(peer));memset(&ble_peer,0,sizeof(ble_peer));
        ble_owner=false;prefer_ble=false;wifi_seen=ble_seen=0;owner_seen=0;owner_client[0]=0;
        last_sent_ms=0;++owner_generation;
    }
    release_after_send=false;xSemaphoreGive(peer_mutex);
    if (released) {
        xSemaphoreTake(v_mutex,portMAX_DELAY);v_status.ble_peer=false;
        bool trusted=vibe_trusted_has(&v_computers,old_client);xSemaphoreGive(v_mutex);
        /* Notify the previous endpoints immediately. Otherwise an idle client
         * can display Connected until its next heartbeat after device unpair. */
        if (old_wifi.sin_port) hello_to(&old_wifi,false,false,true,trusted);
        if (had_ble) hello_to(&old_ble,true,false,true,trusted);
    }
}
