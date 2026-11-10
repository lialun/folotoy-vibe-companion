#include "radio_memory.h"
#include <cassert>
#include <cstdio>
static unsigned attempt,live,fail_at;
static void *allocate(size_t bytes) {
    // A fragmented heap can satisfy the individual blocks but not a 30 KiB one.
    assert(bytes<=8192);
    if (++attempt==fail_at) return nullptr;
    void *p=std::malloc(bytes);assert(p);++live;return p;
}
static void release(void *p) { if (p) { assert(live);--live;std::free(p); } }
static void *refuse_resize(void *,size_t) { return nullptr; }
int main() {
    for (unsigned mp3=0;mp3<2;mp3++) for (unsigned failure=0;failure<=(mp3?6:5);failure++) {
        attempt=live=0;fail_at=failure;
        {
            radio_stream::Workspace workspace(release);
            bool success=failure==0 || failure>=5;
            assert(workspace.allocate(allocate,mp3)==success);
            if (success) {
                unsigned blocks=failure?failure-4:mp3?3:2;
                assert(live==blocks+3 && workspace.input && workspace.output && workspace.stack);
                assert(workspace.fifo_bytes()==blocks*8192);
                assert(workspace.output_capacity==(mp3?4608:8192));
                for (unsigned i=0;i<workspace.fifo_count;i++) workspace.fifo[i][8191]=1;
                workspace.input[1023]=2;workspace.output[workspace.output_capacity-1]=3;workspace.stack[5119]=4;
            }
        }
        assert(live==0);
    }
    for (unsigned mp3=0;mp3<2;mp3++) {
        attempt=live=fail_at=0;
        {
            radio_stream::Workspace workspace(release);
            assert(workspace.allocate_decode(allocate,mp3));
            assert(live==2 && workspace.input && workspace.output && !workspace.stack);
            for (auto *block:workspace.fifo) assert(!block);
            // Container parser/codec preparation happens here, before the FIFO
            // or reader stack can consume/fragment the codec's large block.
            assert(workspace.allocate_stream(allocate));
            assert(live==(mp3?6:5));
        }
        assert(!live);
    }
    {
        attempt=live=fail_at=0;
        radio_stream::Workspace w(release);
        assert(w.allocate_decode(allocate));
        w.output[0]=42;w.output[4095]=43;
        assert(!w.compact_output(4096,refuse_resize) && w.output_capacity==8192);
        assert(w.compact_output(8192) && w.output_capacity==8192);
        assert(w.compact_output(4096) && w.output_capacity==4608);
        assert(w.output[0]==42 && w.output[4095]==43 && live==2);
        const size_t full=w.stack_size+w.network_reserve+2*w.fifo_block_size;
        assert(w.plan_stream(full) && w.fifo_count==2);
        assert(w.plan_stream(full-1) && w.fifo_count==1);
        assert(!w.plan_stream(w.stack_size+w.network_reserve+w.fifo_block_size-1));
    }
    assert(!live);
    // Actual free heap after codec/I2S determines the same 24/16/8 KiB
    // policy for AAC/TS and MP3, with exact admission-boundary coverage.
    for (unsigned mp3=0;mp3<2;mp3++) for (unsigned blocks=1;blocks<=3;blocks++) {
        const size_t threshold=radio_stream::Workspace::stack_size+radio_stream::Workspace::network_reserve+blocks*8192;
        for (int delta=-1;delta<=1;delta++) {
            attempt=live=fail_at=0;
            { radio_stream::Workspace w(release);assert(w.allocate_decode(allocate,mp3));
              bool admitted=w.plan_stream(threshold+delta);
              assert(admitted==(blocks>1 || delta>=0));
              if (admitted) {
                assert(w.fifo_count==blocks-(delta<0?1:0));assert(w.allocate_stream(allocate));
                assert(w.retain_reserve(w.network_reserve));
              }
            }
            assert(!live);
        }
    }
    for (unsigned failure=0;failure<=6;failure++) {
        attempt=live=0;fail_at=failure;
        { radio_stream::Workspace w(release);
          bool ready=w.allocate_decode(allocate);
          if (ready) {
            assert(w.plan_stream(100000));ready=w.allocate_stream(allocate);
          }
          assert(ready==(failure==0 || failure>=5));
          if (ready) {
            assert(w.fifo_count==(failure?failure-4:3));
            unsigned before=w.fifo_count;
            assert(w.retain_reserve(w.network_reserve-1)==(before>1));
            if (!failure) {
                assert(w.fifo_count==2 && !w.fifo[2]);
                assert(!w.retain_reserve(w.network_reserve-8192-1) && w.fifo_count==1 && !w.fifo[1]);
                assert(!w.retain_reserve(w.network_reserve-1));
            }
          }
        }
        assert(!live);
    }
    puts("Radio workspace: fragmented-block sizing and every partial-allocation cleanup PASS");
}
