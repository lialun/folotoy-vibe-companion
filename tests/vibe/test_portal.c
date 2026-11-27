#include "portal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static int local_socket(struct sockaddr_in *address) {
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(fd>=0);
    *address=(struct sockaddr_in){.sin_family=AF_INET};
    assert(inet_pton(AF_INET,"127.0.0.1",&address->sin_addr)==1);
    assert(bind(fd,(struct sockaddr *)address,sizeof(*address))==0);
    socklen_t size=sizeof(*address);assert(getsockname(fd,(struct sockaddr *)address,&size)==0);
    return fd;
}
static void phone_probe_burst(void) {
    struct sockaddr_in dns_address,data_address,phone_address;
    int dns=local_socket(&dns_address),data=local_socket(&data_address),phone=local_socket(&phone_address);
    uint8_t query[]={0x44,0,1,0,0,1,0,0,0,0,0,0,7,'c','a','p','t','i','v','e',
        5,'a','p','p','l','e',3,'c','o','m',0,0,1,0,1};
    uint8_t oversized[513]={0};
    /* Invalid/empty datagrams cannot hold up valid A and AAAA probes. */
    assert(sendto(phone,"",0,0,(struct sockaddr *)&dns_address,sizeof(dns_address))==0);
    assert(sendto(phone,oversized,sizeof(oversized),0,(struct sockaddr *)&dns_address,sizeof(dns_address))==(int)sizeof(oversized));
    for (unsigned i=0;i<18;i++) {
        query[1]=i;query[sizeof(query)-3]=i%2?28:1;
        assert(sendto(phone,query,sizeof(query),0,(struct sockaddr *)&dns_address,sizeof(dns_address))==(int)sizeof(query));
    }
    assert(sendto(phone,"CTRL",4,0,(struct sockaddr *)&data_address,sizeof(data_address))==4);
    /* Loopback delivery may be asynchronous: wait for the control datagram
     * independently before checking the combined readiness mask. */
    assert(vibe_portal_wait(data,-1,1000)==VIBE_PORTAL_DATA_READY);
    assert(vibe_portal_wait(dns,-1,1000)==VIBE_PORTAL_DATA_READY);
    assert(vibe_portal_wait(data,dns,1000)==(VIBE_PORTAL_DATA_READY|VIBE_PORTAL_DNS_READY));
    unsigned consumed=vibe_portal_dns_poll(dns);
    assert(consumed>0 && consumed<=VIBE_PORTAL_DNS_BATCH);
    uint8_t reply[512];assert(recv(data,reply,sizeof(reply),MSG_DONTWAIT)==4 && !memcmp(reply,"CTRL",4));
    for (unsigned attempts=0;consumed<20 && attempts<20;attempts++) {
        assert(vibe_portal_wait(data,dns,1000)==VIBE_PORTAL_DNS_READY);
        unsigned batch=vibe_portal_dns_poll(dns);
        assert(batch>0 && batch<=VIBE_PORTAL_DNS_BATCH);consumed+=batch;
    }
    assert(consumed==20);
    assert(vibe_portal_dns_poll(dns)==0);
    unsigned received=0,identities=0;
    for (;received<18;received++) {
        assert(vibe_portal_wait(phone,-1,1000)==VIBE_PORTAL_DATA_READY);
        int n=recv(phone,reply,sizeof(reply),MSG_DONTWAIT);
        assert(n>=(int)sizeof(query));assert(reply[0]==0x44 && reply[1]<18);
        assert(!(identities&(1u<<reply[1])));identities|=1u<<reply[1];
        assert(reply[7]==(reply[1]%2?0:1));
    }
    assert(received==18 && identities==((1u<<18)-1));
    assert(vibe_portal_wait(data,dns,0)==0);
    /* DNS alone wakes the same wait; no control packet or timer is needed. */
    assert(sendto(phone,query,sizeof(query),0,(struct sockaddr *)&dns_address,sizeof(dns_address))==(int)sizeof(query));
    assert(vibe_portal_wait(data,dns,1000)==VIBE_PORTAL_DNS_READY);
    assert(vibe_portal_dns_poll(dns)==1);
    assert(vibe_portal_wait(phone,-1,1000)==VIBE_PORTAL_DATA_READY);
    assert(recv(phone,reply,sizeof(reply),MSG_DONTWAIT)>0);
    assert(vibe_portal_wait(data,-1,0)==0);
    assert(vibe_portal_wait(-1,dns,0)==-1 && vibe_portal_dns_poll(-1)==0);
    close(phone);close(data);close(dns);
}

int main(void) {
    phone_probe_burst();
    assert(!vibe_portal_accepts_gzip(NULL) && !vibe_portal_accepts_gzip("") && !vibe_portal_accepts_gzip("br, identity"));
    assert(vibe_portal_accepts_gzip("gzip, deflate, br") && vibe_portal_accepts_gzip("GZip ; q=0.125"));
    assert(vibe_portal_accepts_gzip("*;q=1") && !vibe_portal_accepts_gzip("gzip;q=0,*;q=1"));
    assert(vibe_portal_accepts_gzip("gzip;q=1.000") && !vibe_portal_accepts_gzip("gzip;q=1.001"));
    assert(!vibe_portal_accepts_gzip("gzip;q=0.000") && !vibe_portal_accepts_gzip("gzip;q=0.0001"));
    assert(!vibe_portal_accepts_gzip("gzip;q=2") && !vibe_portal_accepts_gzip("gzip;q=foo"));
    assert(!vibe_portal_accepts_gzip("notgzip") && vibe_portal_accepts_gzip("br, gzip;q=0.5, deflate"));

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
    puts("Captive portal: DNS-only wake, bounded phone probe bursts, concurrent control, IPv4/IPv6, origin and DNS/EDNS PASS");
}
