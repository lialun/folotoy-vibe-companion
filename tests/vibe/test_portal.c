#include "portal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void) {
    assert(!vibe_portal_close_due(true,0,5000));
    assert(!vibe_portal_close_due(true,1000,3999));
    assert(vibe_portal_close_due(true,1000,4000));
    assert(vibe_portal_close_due(true,1000,9000));
    assert(!vibe_portal_close_due(false,1000,4000));
    assert(!vibe_portal_close_due(true,4000,1000));
    struct sockaddr_in ipv4={.sin_family=AF_INET};
    inet_pton(AF_INET,"192.168.4.1",&ipv4.sin_addr);
    assert(vibe_portal_local_address((struct sockaddr *)&ipv4,sizeof(ipv4)));
    inet_pton(AF_INET,"192.168.1.1",&ipv4.sin_addr);
    assert(!vibe_portal_local_address((struct sockaddr *)&ipv4,sizeof(ipv4)));
    struct sockaddr_in6 ipv6={.sin6_family=AF_INET6};
    inet_pton(AF_INET6,"::ffff:192.168.4.1",&ipv6.sin6_addr);
    assert(vibe_portal_local_address((struct sockaddr *)&ipv6,sizeof(ipv6)));
    assert(!vibe_portal_local_address((struct sockaddr *)&ipv6,sizeof(ipv4)));
    inet_pton(AF_INET6,"::ffff:192.168.1.1",&ipv6.sin6_addr);
    assert(!vibe_portal_local_address((struct sockaddr *)&ipv6,sizeof(ipv6)));
    inet_pton(AF_INET6,"::1",&ipv6.sin6_addr);
    assert(!vibe_portal_local_address((struct sockaddr *)&ipv6,sizeof(ipv6)));
    assert(!vibe_portal_local_address(NULL,0));
    assert(vibe_portal_host("192.168.4.1:80"));
    assert(!vibe_portal_host("192.168.4.1.attacker.example"));
    assert(vibe_portal_origin("http://192.168.4.1"));
    assert(!vibe_portal_origin("http://192.168.4.1:8000"));
    uint8_t query[]={0x12,0x34,1,0x30,0,1,0,0,0,0,0,1,3,'w','w','w',7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1,
        0,0,41,4,208,0,0,0,0,0,0};
    uint8_t reply[512];
    size_t n=vibe_portal_dns_reply(query,sizeof(query),reply,sizeof(reply));
    assert(n==49 && reply[0]==0x12 && reply[1]==0x34 && reply[7]==1 && !reply[11]);
    assert(reply[33]==0xc0 && reply[34]==0x0c);
    assert(!memcmp(reply+n-4,(uint8_t[]){192,168,4,1},4));
    assert(!vibe_portal_dns_reply(query,sizeof(query),reply,n-1));
    for (size_t i=0;i<33;i++) assert(!vibe_portal_dns_reply(query,i,reply,sizeof(reply)));
    query[30]=28; /* AAAA: valid NODATA reply, not an invalid IPv4 answer. */
    assert(vibe_portal_dns_reply(query,sizeof(query),reply,sizeof(reply))==33 && !reply[7]);
    query[30]=1;query[12]=0xc0;
    assert(!vibe_portal_dns_reply(query,sizeof(query),reply,sizeof(reply)));
    query[12]=3;query[2]|=0x80;
    assert(!vibe_portal_dns_reply(query,sizeof(query),reply,sizeof(reply)));
    puts("Captive portal: IPv4/mapped IPv6, origin, DNS/EDNS and malformed queries PASS");
}
