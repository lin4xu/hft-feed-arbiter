#pragma once

#include <atomic>
#include <cstddef>
#include "../utils/cache_line_pad.hpp"

namespace hft {

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");

private:
    static constexpr std::size_t MASK = Capacity - 1;
    T buffer_[Capacity];

    CacheLinePad<std::atomic<std::size_t>> tail_{0};
    CacheLinePad<std::atomic<std::size_t>> head_{0};

public:
    SPSCQueue() = default;

    bool try_push(const T& item) {
        const std::size_t current_tail = tail_->load(std::memory_order_relaxed);
        const std::size_t current_head = head_->load(std::memory_order_acquire);

        if (current_tail - current_head >= Capacity) {
            return false; 
        }

        buffer_[current_tail & MASK] = item;
        tail_->store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out_item) {
        const std::size_t current_head = head_->load(std::memory_order_relaxed);
        const std::size_t current_tail = tail_->load(std::memory_order_acquire);

        if (current_tail == current_head) {
            return false; 
        }

        out_item = buffer_[current_head & MASK];
        head_->store(current_head + 1, std::memory_order_release);
        return true;
    }
};

} // namespace hft