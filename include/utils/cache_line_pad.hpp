#pragma once

#include <new>

namespace hft {

// 强制按 64 字节（主流 CPU 的 Cache Line 大小）对齐
template <typename T>
struct alignas(64) CacheLinePad {
    T data;

    CacheLinePad() = default;
    
    template <typename... Args>
    CacheLinePad(Args&&... args) : data(std::forward<Args>(args)...) {}

    T* operator->() { return &data; }
    const T* operator->() const { return &data; }
    T& operator*() { return data; }
    const T& operator*() const { return data; }
};

} // namespace hft