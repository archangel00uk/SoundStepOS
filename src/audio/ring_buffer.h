#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

// The producer/consumer indices are deliberately cache-line aligned to avoid
// false sharing; the resulting struct padding is expected.
#ifdef _MSC_VER
#pragma warning(disable : 4324)
#endif

namespace sso::audio {

// Single-producer / single-consumer lock-free ring buffer.
// Capacity is rounded up to the next power of two. Indices grow monotonically
// (64-bit) and are masked for the backing array, so there is no ABA-style
// ambiguity. Producer and consumer indices live on separate cache lines.
template <typename T>
class SPSCRingBuffer {
public:
    explicit SPSCRingBuffer(std::size_t capacity) {
        std::size_t c = 1;
        while (c < capacity) c <<= 1;
        mask_ = c - 1;
        buf_.resize(c);
    }

    std::size_t capacity() const { return buf_.size(); }

    std::size_t size() const {
        return write_.load(std::memory_order_acquire) -
               read_.load(std::memory_order_acquire);
    }

    // Returns false (and writes nothing) if there is not enough room.
    bool push(const T* src, std::size_t n) {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        if (n > capacity() - (w - r)) return false;
        for (std::size_t i = 0; i < n; ++i) buf_[(w + i) & mask_] = src[i];
        write_.store(w + n, std::memory_order_release);
        return true;
    }

    // Returns false (and reads nothing) if fewer than n elements are available.
    bool pop(T* dst, std::size_t n) {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        if (n > w - r) return false;
        for (std::size_t i = 0; i < n; ++i) dst[i] = buf_[(r + i) & mask_];
        read_.store(r + n, std::memory_order_release);
        return true;
    }

private:
    std::size_t mask_;
    std::vector<T> buf_;
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
};

} // namespace sso::audio
