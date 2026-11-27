/* Both v_mutex and peer_mutex are held. Trust is persistent; this is only
 * the active session. Old commands/audio lose their generation at handoff. */
static void select_owner_locked(const char *client,const char *token) {
    memset(&peer,0,sizeof(peer));memset(&ble_peer,0,sizeof(ble_peer));
    ble_owner=false;prefer_ble=false;wifi_seen=ble_seen=last_sent_ms=0;
    strlcpy(owner_client,client,sizeof(owner_client));owner_seen=vibe_now();
    if (!++owner_generation) ++owner_generation;
    release_after_send=false;
    if (token!=v_status.token) strlcpy(v_status.token,token,sizeof(v_status.token));
    v_status.peer=false;v_status.peer_seen_ms=owner_seen;v_status.ble_peer=false;
    v_status.audio_ready=false;v_status.hotkey_ready=false;v_status.radio_pcm=false;
    v_status.voice_v2=false;v_status.voice_repair=false;v_status.voice_handshake=false;
    v_status.voice_interleave=false;v_status.voice_feedback=false;
    memset(&v_quotas,0,sizeof(v_quotas));
}
