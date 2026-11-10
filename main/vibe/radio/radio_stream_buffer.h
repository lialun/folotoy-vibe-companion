#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace radio_stream {
// Single producer (HTTP) / single consumer (decoder). Positions wrap at twice
// capacity, so non-power-of-two segmented storage is safe even after days of
// streaming. The session owns all blocks and outlives both workers.
class Buffer {
public:
    Buffer(uint8_t *storage, std::size_t capacity) : data_(storage),blocks_(nullptr),capacity_(capacity),block_size_(capacity) {
        assert(storage && capacity && capacity<=UINT32_MAX/2);
    }
    Buffer(uint8_t *const *blocks,std::size_t block_size,unsigned count)
        : data_(nullptr),blocks_(blocks),capacity_(block_size*count),block_size_(block_size) {
        assert(blocks && count && block_size && block_size<=UINT32_MAX/2/count);
        for (unsigned i=0;i<count;i++) assert(blocks[i]);
    }
    std::size_t available() const {
        const uint32_t w=write_.load(std::memory_order_acquire),r=read_.load(std::memory_order_acquire);
        return w>=r?w-r:capacity_*2-r+w;
    }
    uint8_t *write_data(std::size_t &contiguous) {
        const auto written = write_.load(std::memory_order_relaxed);
        const auto consumed=read_.load(std::memory_order_acquire);
        const auto used=written>=consumed?written-consumed:capacity_*2-consumed+written;
        const auto offset = written % capacity_;
        contiguous = std::min<std::size_t>(capacity_ - used, block_size_ - offset%block_size_);
        return at(offset);
    }
    void commit(std::size_t bytes) {
        write_.store(advance(write_.load(std::memory_order_relaxed),bytes),std::memory_order_release);
    }
    std::size_t read(uint8_t *target, std::size_t capacity) {
        const auto consumed = read_.load(std::memory_order_relaxed);
        const auto bytes = std::min<std::size_t>(capacity,available());
        auto offset=consumed%capacity_;
        for (std::size_t copied=0;copied<bytes;) {
            const auto chunk=std::min<std::size_t>(bytes-copied,block_size_-offset%block_size_);
            std::memcpy(target+copied,at(offset),chunk);copied+=chunk;offset=(offset+chunk)%capacity_;
        }
        read_.store(advance(consumed,bytes), std::memory_order_release);
        return bytes;
    }
private:
    uint8_t *data_;
    uint8_t *const *blocks_;
    const std::size_t capacity_,block_size_;
    std::atomic<uint32_t> read_{0}, write_{0};
    uint8_t *at(std::size_t offset) const { return blocks_?blocks_[offset/block_size_]+offset%block_size_:data_+offset; }
    uint32_t advance(uint32_t position,std::size_t bytes) const {
        const auto next=position+bytes;return next>=capacity_*2?next-capacity_*2:next;
    }
};

// Some broadcasters send ICY metadata even when the request opts out. Strip it
// before MP3 parsing, including length/data split across reads or FIFO wraps.
class IcyFilter {
public:
    explicit IcyFilter(uint32_t interval = 0) : interval_(interval), audio_left_(interval) {}
    std::size_t filter(uint8_t *data, std::size_t bytes) {
        if (!interval_) return bytes;
        std::size_t out = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            if (metadata_left_) {
                if (--metadata_left_ == 0) audio_left_ = interval_;
            } else if (audio_left_) {
                data[out++] = data[i];
                --audio_left_;
            } else {
                metadata_left_ = static_cast<uint32_t>(data[i]) * 16;
                if (!metadata_left_) audio_left_ = interval_;
            }
        }
        return out;
    }
private:
    const uint32_t interval_;
    uint32_t audio_left_, metadata_left_{0};
};
}  // namespace radio_stream
