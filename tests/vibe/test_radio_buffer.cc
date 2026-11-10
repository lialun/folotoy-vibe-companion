#include "radio_stream_buffer.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <random>
#include <thread>
#include <vector>
static void model(radio_stream::Buffer &fifo,size_t capacity) {
    std::mt19937 random(27);std::deque<uint8_t> expected;
    std::vector<uint8_t> out(1703);unsigned serial=0;
    for (unsigned i=0;i<100000;i++) {
        if (random()%2) {
            size_t room=0;auto *target=fifo.write_data(room);
            assert(room<=capacity-expected.size());
            const auto n=std::min<size_t>(room,random()%1501);
            for (size_t j=0;j<n;j++) { auto value=uint8_t(serial++);target[j]=value;expected.push_back(value); }
            fifo.commit(n);
        } else {
            const auto n=fifo.read(out.data(),random()%out.size());
            for (size_t j=0;j<n;j++) { assert(!expected.empty() && out[j]==expected.front());expected.pop_front(); }
        }
        assert(fifo.available()==expected.size() && expected.size()<=capacity);
    }
}
int main() {
    uint8_t linear[16]{};radio_stream::Buffer one(linear,sizeof(linear));model(one,sizeof(linear));
    uint8_t guarded[3][259];std::memset(guarded,0xa5,sizeof(guarded));
    uint8_t *small[]={guarded[0]+1,guarded[1]+1,guarded[2]+1};
    radio_stream::Buffer split(small,257,3);model(split,771);
    for (auto &block:guarded) assert(block[0]==0xa5 && block[258]==0xa5);
    uint8_t storage[3][8192];uint8_t *blocks[]={storage[0],storage[1],storage[2]};
    radio_stream::Buffer live(blocks,8192,3);
    constexpr size_t total=2*1024*1024;
    std::thread producer([&] {
        for (size_t sent=0;sent<total;) {
            size_t room;auto *target=live.write_data(room);
            auto n=std::min({room,size_t(257),total-sent});
            if (!n) { std::this_thread::yield();continue; }
            for (size_t i=0;i<n;i++) target[i]=(sent+i)%251;
            live.commit(n);sent+=n;
        }
    });
    uint8_t out[773];
    for (size_t read=0;read<total;) {
        const auto n=live.read(out,sizeof(out));
        if (!n) { std::this_thread::yield();continue; }
        for (size_t i=0;i<n;i++) assert(out[i]==(read+i)%251);
        read+=n;
    }
    producer.join();assert(live.available()==0);
    puts("Radio FIFO: segmented boundaries, bounded wraparound, guards, reference model and concurrent 24 KiB streaming PASS");
}
