#include "portal.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

bool vibe_portal_local_address(const struct sockaddr *address,size_t length) {
    static const uint8_t ipv4[]={192,168,4,1};
    static const uint8_t mapped[]={0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,1};
    if (!address || length<sizeof(struct sockaddr)) return false;
    if (address->sa_family==AF_INET && length>=sizeof(struct sockaddr_in))
        return !memcmp(&((const struct sockaddr_in *)address)->sin_addr,ipv4,sizeof(ipv4));
    if (address->sa_family==AF_INET6 && length>=sizeof(struct sockaddr_in6))
        return !memcmp(&((const struct sockaddr_in6 *)address)->sin6_addr,mapped,sizeof(mapped));
    return false;
}
bool vibe_portal_host(const char *host) {
    return host && (!strcmp(host,VIBE_PORTAL_IP) || !strcmp(host,VIBE_PORTAL_IP ":80"));
}
bool vibe_portal_origin(const char *origin) {
    return origin && (!strcmp(origin,"http://" VIBE_PORTAL_IP) || !strcmp(origin,"http://" VIBE_PORTAL_IP ":80"));
}
bool vibe_portal_close_due(bool wifi_connected,uint64_t saved_at,uint64_t now) {
    /* Allow status polling to observe a successful save, but never depend on
     * a second phone request to shut down the setup hotspot. */
    return wifi_connected && saved_at && now>=saved_at && now-saved_at>=3000;
}
static unsigned be16(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }
size_t vibe_portal_dns_reply(const uint8_t *query,size_t length,uint8_t *reply,size_t capacity) {
    if (!query || !reply || length<17 || length>512 || capacity<12 ||
        (be16(query+2)&0xf800) || be16(query+4)!=1 || be16(query+6) || be16(query+8)) return 0;
    size_t end=12;
    while (end<length && query[end]) {
        unsigned label=query[end++];
        if (label>63 || end+label>length || end+label-12>254) return 0;
        end+=label;
    }
    if (end>=length || length-end<5) return 0;
    ++end;
    unsigned type=be16(query+end),qclass=be16(query+end+2); end+=4;
    if (qclass!=1 || end>capacity) return 0;
    bool answer=type==1 || type==255;
    if (answer && capacity-end<16) return 0;
    /* Drop additional/EDNS records, then append at the actual question end.
     * In particular AD/CD flags must not be mistaken for the opcode. */
    memcpy(reply,query,end);
    reply[2]=0x84 | (query[2]&1); reply[3]=0;
    reply[6]=0; reply[7]=answer?1:0;
    memset(reply+8,0,4);
    if (answer) {
        static const uint8_t record[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4,192,168,4,1};
        memcpy(reply+end,record,sizeof(record)); end+=sizeof(record);
    }
    return end;
}
