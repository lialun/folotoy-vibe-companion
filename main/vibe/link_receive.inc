#include "radio/remote.h"
#include "radio/stream.h"
/* Shared ingress. Discovery/approval never acquires the business session. */
static const char *link_string(cJSON *o,const char *key) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:NULL;
}
static void connection_reply(const struct sockaddr_in *from,bool ble,const char *request,const char *state) {
    if (!vibe_connect_identity(request)) return;
    char line[192];int n=snprintf(line,sizeof(line),"{\"event\":\"connection.reply\",\"request\":\"%s\",\"state\":\"%s\"}",request,state);
    if (ble) (void)vibe_ble_send_to(ntohs(from->sin_port)-1,2,line,n);
    else { unsigned char packet[193];packet[0]=2;memcpy(packet+1,line,n);sendto(udp,packet,n+1,MSG_DONTWAIT,(const struct sockaddr *)from,sizeof(*from)); }
}
static void process_packet(const uint8_t *packet,size_t n,const struct sockaddr_in *from,bool via_ble) {
    if (n && packet[0]==V_VOICE_FEEDBACK_TYPE) {
        vibe_voice_feedback_t feedback;
        if (!vibe_voice_feedback_parse(packet,n,&feedback)) return;
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        bool credential=vibe_voice_feedback_authenticated(packet,v_status.token);
        bool supported=v_status.voice_feedback;
        unsigned heartbeat=v_status.heartbeat_ms;
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        bool endpoint=via_ble?ble_owner && vibe_peer_matches(ble_peer.sin_addr.s_addr,ble_peer.sin_port,from->sin_addr.s_addr,from->sin_port):
            vibe_peer_matches(peer.sin_addr.s_addr,peer.sin_port,from->sin_addr.s_addr,from->sin_port);
        bool allowed=supported && credential && endpoint && owner_client[0] && owner_seen &&
            vibe_now()-owner_seen<vibe_peer_timeout(heartbeat) && !release_after_send;
        uint32_t generation=owner_generation;
        xSemaphoreGive(peer_mutex);xSemaphoreGive(v_mutex);
        if (allowed) (void)vibe_voice_uplink_feedback(&feedback,generation);
        return;
    }
    if (n==V_RADIO_PACKET && packet[0]==5) {
        xSemaphoreTake(peer_mutex,portMAX_DELAY);
        bool allowed=owner_client[0] && (via_ble ? ble_owner && vibe_peer_matches(ble_peer.sin_addr.s_addr,ble_peer.sin_port,from->sin_addr.s_addr,from->sin_port) : vibe_peer_matches(peer.sin_addr.s_addr,peer.sin_port,from->sin_addr.s_addr,from->sin_port));
        uint32_t generation=owner_generation;xSemaphoreGive(peer_mutex);
        if (allowed) vibe_radio_remote_receive(packet,n,generation,via_ble);
        return;
    }
    if (n<2 || n>VIBE_JSON_MAX+1 || (packet[0]!=2 && packet[0]!=3) || !vibe_json_depth_ok((const char *)packet+1,n-1)) return;
    cJSON *o=cJSON_ParseWithLength((const char *)packet+1,n-1);if (!o) return;
    link_snapshot_t s=link_snapshot();uint64_t now=vibe_now();
    const char *token=link_string(o,"token"),*type=link_string(o,"type"),*target=link_string(o,"device");
    const char *client=link_string(o,"client"),*request=link_string(o,"request"),*digest=link_string(o,"digest");
    const char *role=link_string(o,"role");
    bool addressed=target && !strcmp(target,s.device);
    bool discovery=packet[0]==3 && role && !strcmp(role,"pc");
    if (addressed && type && (!strcmp(type,"connection.request") || !strcmp(type,"connection.cancel"))) {
        const char *result="busy";
        xSemaphoreTake(v_mutex,portMAX_DELAY);
        vibe_connection_request_t *r=&v_status.connection_request;
        if (!strcmp(type,"connection.request")) {
            if (!v_model.recording && vibe_connect_begin(r,client,request,digest,link_string(o,"name"),v_status.token[0]!=0,now)) result=vibe_connect_state(r);
        } else if (vibe_connect_matches(r,client,request,digest)) { r->phase=V_CONNECT_CANCELLED;result="cancelled"; }
        xSemaphoreGive(v_mutex);
        vibe_msg_t message={.type=V_MSG_CONNECTION_CHANGED};(void)xQueueSend(v_queue,&message,0);
        connection_reply(from,via_ble,request,result);cJSON_Delete(o);return;
    }
    bool authenticated=false,claimed=false;uint32_t generation=0;
    xSemaphoreTake(v_mutex,portMAX_DELAY);
    authenticated=token && v_status.token[0] && !strcmp(token,v_status.token);
    bool approved=addressed && type && !strcmp(type,"pair") && vibe_connection_approved(token,client,request);
    xSemaphoreTake(peer_mutex,portMAX_DELAY);
    bool pair=addressed && type && !strcmp(type,"pair") && vibe_connect_identity(client) &&
        (approved || (authenticated && owner_client[0] && !strcmp(owner_client,client)));
    bool same_client=vibe_connect_identity(client) && owner_client[0] && !strcmp(owner_client,client);
    bool occupied=owner_seen && now-owner_seen<vibe_peer_timeout(s.heartbeat);
    if(type && (!strcmp(type,"voice.repair") || !strcmp(type,"voice.ready") || !strcmp(type,"voice.cancel"))) {
        /* Repairs cannot claim a lease or enter the UI command queue. Only the
         * current authenticated endpoint may request this recording's cache. */
        bool endpoint=via_ble?ble_owner && vibe_peer_matches(ble_peer.sin_addr.s_addr,ble_peer.sin_port,from->sin_addr.s_addr,from->sin_port):
            vibe_peer_matches(peer.sin_addr.s_addr,peer.sin_port,from->sin_addr.s_addr,from->sin_port);
        bool allowed=packet[0]==2 && addressed && authenticated && same_client && occupied && endpoint && !release_after_send;
        generation=owner_generation;
        xSemaphoreGive(peer_mutex);xSemaphoreGive(v_mutex);
        if (!strcmp(type,"voice.cancel")) {
            if (allowed) vibe_voice_uplink_cancel(vibe_voice_parse_session(link_string(o,"voiceSession")),generation);
            cJSON_Delete(o);return;
        }
        if (!strcmp(type,"voice.ready")) {
            if (allowed) vibe_voice_uplink_ready(vibe_voice_parse_session(link_string(o,"voiceSession")),generation);
            cJSON_Delete(o);return;
        }
        uint32_t frames[VIBE_VOICE_MAX_REPAIR];unsigned count=0;bool valid=true;
        uint32_t request_id=0;
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(o,"repairID");
        if (id) {
            if (!cJSON_IsNumber(id) || !(id->valuedouble>=1 && id->valuedouble<=UINT32_MAX) ||
                id->valuedouble!=(double)(uint32_t)id->valuedouble) valid=false;
            else request_id=(uint32_t)id->valuedouble;
        }
        const cJSON *items=cJSON_GetObjectItemCaseSensitive(o,"frames");
        if(!cJSON_IsArray(items) || cJSON_GetArraySize(items)<1 || cJSON_GetArraySize(items)>VIBE_VOICE_MAX_REPAIR)valid=false;
        if(valid)for(const cJSON *item=items->child;item;item=item->next) {
            if(!cJSON_IsNumber(item) || !(item->valuedouble>=0 && item->valuedouble<VIBE_VOICE_MAX_FRAMES) ||
               item->valuedouble!=(double)(uint32_t)item->valuedouble){valid=false;break;}
            frames[count++]=(uint32_t)item->valuedouble;
        }
        if(allowed && valid)(void)vibe_voice_repair_request(vibe_voice_parse_session(link_string(o,"voiceSession")),frames,count,generation,request_id);
        cJSON_Delete(o);return;
    }
    if (pair && vibe_message_post_link_json(v_queue,(const char *)packet+1,n-1,0)) {
        if (approved) { commit_peer=*from;commit_ble=via_ble; }
    } else if (addressed && authenticated && (!occupied || same_client) && vibe_connect_identity(client)) {
        bool new_owner=!same_client;
        generation=owner_generation+(new_owner?1:0);if (!generation) generation=1;
        if (discovery || vibe_message_post_link_json(v_queue,(const char *)packet+1,n-1,generation)) {
            if (new_owner) { memset(&peer,0,sizeof(peer));memset(&ble_peer,0,sizeof(ble_peer));ble_owner=false;prefer_ble=false;wifi_seen=ble_seen=0; }
            owner_generation=generation;strlcpy(owner_client,client,sizeof(owner_client));owner_seen=now;
            if (via_ble) { ble_peer=*from;ble_owner=true;ble_seen=now; } else { peer=*from;wifi_seen=now; }
            if (discovery && type && !strcmp(type,"pc.status")) {
                const char *primary=link_string(o,"primaryTransport");
                if (primary && (!strcmp(primary,"wifi") || !strcmp(primary,"bluetooth")))
                    prefer_ble=!strcmp(primary,"bluetooth");
            }
            v_status.ble_peer=ble_owner;v_status.peer_seen_ms=now;claimed=true;
        }
    }
    if (via_ble) {
        static unsigned debug;
        if (debug++<4) ESP_LOGI("vibe_ble","Ingress addressed=%d authenticated=%d same_client=%d claimed=%d",addressed,authenticated,same_client,claimed);
    }
    xSemaphoreGive(peer_mutex);xSemaphoreGive(v_mutex);
    if (claimed && discovery && type && !strcmp(type,"pc.status"))
        (void)vibe_message_post_link_json(v_queue,(const char *)packet+1,n-1,generation);
    if (discovery && (!target || addressed)) hello_to(from,via_ble,authenticated && claimed,token!=NULL);
    cJSON_Delete(o);
}
