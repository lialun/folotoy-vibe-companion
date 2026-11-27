#include "portal.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
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

int vibe_portal_wait(int data_socket,int dns_socket,unsigned timeout_ms) {
    if (data_socket<0) return -1;
    fd_set readable;FD_ZERO(&readable);FD_SET(data_socket,&readable);
    int maximum=data_socket;
    if (dns_socket>=0) { FD_SET(dns_socket,&readable);if (dns_socket>maximum) maximum=dns_socket; }
    struct timeval timeout={.tv_sec=timeout_ms/1000,.tv_usec=(timeout_ms%1000)*1000};
    int ready=select(maximum+1,&readable,NULL,NULL,&timeout);
    if (ready<=0) return ready;
    return (FD_ISSET(data_socket,&readable)?VIBE_PORTAL_DATA_READY:0) |
        (dns_socket>=0 && FD_ISSET(dns_socket,&readable)?VIBE_PORTAL_DNS_READY:0);
}
unsigned vibe_portal_dns_poll(int dns_socket) {
    if (dns_socket<0) return 0;
    static uint8_t query[513],reply[512];
    unsigned received=0;
    for (;received<VIBE_PORTAL_DNS_BATCH;received++) {
        struct sockaddr_in from;socklen_t from_len=sizeof(from);
        int n=recvfrom(dns_socket,query,sizeof(query),MSG_DONTWAIT,(struct sockaddr *)&from,&from_len);
        if (n<0) break;
        size_t count=vibe_portal_dns_reply(query,(size_t)n,reply,sizeof(reply));
        if (count) sendto(dns_socket,reply,count,MSG_DONTWAIT,(struct sockaddr *)&from,from_len);
    }
    return received;
}

static bool coding(const char *begin,const char *end,const char *name) {
    size_t n=strlen(name);if ((size_t)(end-begin)!=n) return false;
    for (size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)begin[i];if (c>='A' && c<='Z') c+='a'-'A';
        if (c!=(unsigned char)name[i]) return false;
    }
    return true;
}
static bool header_space(char c) {return c==' ' || c=='\t';}
static int coding_quality(const char *begin,const char *end) {
    while (begin<end && header_space(*begin)) ++begin;
    if (begin==end) return 1000;
    if (*begin++!=';') return 0;
    while (begin<end && header_space(*begin)) ++begin;
    if (begin==end || (*begin!='q' && *begin!='Q')) return 0;
    ++begin;while (begin<end && header_space(*begin)) ++begin;
    if (begin==end || *begin++!='=') return 0;
    while (begin<end && header_space(*begin)) ++begin;
    if (begin==end || (*begin!='0' && *begin!='1')) return 0;
    int whole=*begin++-'0',fraction=0,scale=100;
    if (begin<end && *begin=='.') {
        ++begin;
        while (begin<end && *begin>='0' && *begin<='9') {
            if (!scale || (whole && *begin!='0')) return 0;
            fraction+=(*begin++-'0')*scale;scale/=10;
        }
    }
    while (begin<end && header_space(*begin)) ++begin;
    return begin==end?whole*1000+fraction:0;
}
bool vibe_portal_accepts_gzip(const char *header) {
    if (!header) return false;
    int gzip=-1,wildcard=-1;
    for (const char *at=header;*at;) {
        const char *end=strchr(at,',');if (!end) end=at+strlen(at);
        while (at<end && header_space(*at)) ++at;
        const char *token=at;while (at<end && !header_space(*at) && *at!=';') ++at;
        int quality=coding_quality(at,end);
        if (coding(token,at,"gzip")) gzip=quality;
        else if (coding(token,at,"*")) wildcard=quality;
        at=*end?end+1:end;
    }
    return (gzip>=0?gzip:wildcard)>0;
}
