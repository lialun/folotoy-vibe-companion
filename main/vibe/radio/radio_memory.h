#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace radio_stream {
// Independent blocks fit fragmented internal SRAM. Size the FIFO only after
// the actual decoder, first PCM frame and I2S are live, keeping network margin.
struct Workspace {
    static constexpr size_t fifo_block_size=8192,input_size=1024,output_size=8192,stack_size=5120;
    using Allocate=void *(*)(size_t);
    using Release=void (*)(void *);
    using Resize=void *(*)(void *,size_t);
    static constexpr size_t network_reserve=16*1024;
    uint8_t *fifo[3]{},*input=nullptr,*output=nullptr,*stack=nullptr;
    unsigned fifo_count=2;
    size_t output_capacity=output_size;
    Release release;
    explicit Workspace(Release free_block=std::free):release(free_block) {}
    Workspace(const Workspace &)=delete;
    Workspace &operator=(const Workspace &)=delete;
    size_t fifo_bytes() const { return fifo_count*fifo_block_size; }
    bool allocate_decode(Allocate allocate_block=std::malloc,bool mp3=false) {
        fifo_count=mp3?3:2;output_capacity=mp3?4608:output_size;
        output=static_cast<uint8_t *>(allocate_block(output_capacity));
        if (output) input=static_cast<uint8_t *>(allocate_block(input_size));
        return input!=nullptr;
    }
    // First-frame PCM is preserved by realloc. AAC-LC and MP3 fit 4,608 B;
    // retain the original 8 KiB for a source whose first frame needs more.
    bool compact_output(size_t decoded_bytes,Resize resize=std::realloc) {
        if (decoded_bytes>4608 || output_capacity<=4608) return true;
        auto *smaller=static_cast<uint8_t *>(resize(output,4608));
        if (!smaller) return false; // The original allocation is still owned.
        output=smaller;output_capacity=4608;return true;
    }
    // Called after codec and I2S initialization. Never consume the control/
    // network margin merely because another FIFO block can be allocated.
    bool plan_stream(size_t free_bytes) {
        const auto budget=stack_size+network_reserve;
        fifo_count=3;
        while (fifo_count>1 && free_bytes<budget+fifo_bytes()) --fifo_count;
        return free_bytes>=budget+fifo_bytes();
    }
    // Containers can create the real codec lazily on their first audio frame.
    // Decode that frame before the FIFO/reader stack fragment the remaining RAM.
    bool allocate_stream(Allocate allocate_block=std::malloc) {
        // Protect the required contiguous reader stack before optional FIFO
        // growth. A failed second/third block retains the smaller usable FIFO.
        stack=static_cast<uint8_t *>(allocate_block(stack_size));
        if (!stack) return false;
        for (unsigned i=0;i<fifo_count;i++) {
            fifo[i]=static_cast<uint8_t *>(allocate_block(fifo_block_size));
            if (!fifo[i]) { fifo_count=i;return i>0; }
        }
        return true;
    }
    // Include allocator overhead and concurrent small control allocations in
    // the final admission check, before handing pointers to the reader task.
    bool retain_reserve(size_t free_bytes) {
        while (free_bytes<network_reserve && fifo_count>1) {
            release(fifo[--fifo_count]);fifo[fifo_count]=nullptr;
            free_bytes+=fifo_block_size;
        }
        return free_bytes>=network_reserve;
    }
    bool allocate(Allocate allocate_block=std::malloc,bool mp3=false) {
        return allocate_decode(allocate_block,mp3) && allocate_stream(allocate_block);
    }
    ~Workspace() { release(input);release(stack);release(output);for (auto *block:fifo) release(block); }
};
}
