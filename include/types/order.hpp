#pragma once
#include <cstdint>
#include <type_traits>

namespace hft {

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

// 极致紧凑的单合约订单结构体 (总大小 24 Bytes)
struct Order {
    uint32_t order_id;    // 4 Bytes (作为行情仲裁器的去重序列号)
    uint32_t price;       // 4 Bytes (整型定点数价格)
    uint32_t quantity;    // 4 Bytes
    Side side;            // 1 Byte
    // --- 编译器自动插入 3 Bytes Padding ---
    uint64_t timestamp;   // 8 Bytes

    Order() = default;

    Order(uint32_t id, uint32_t p, uint32_t q, Side s, uint64_t ts = 0)
        : order_id(id), price(p), quantity(q), side(s), timestamp(ts) {}
};

// ---------------------------------------------------------
// 内存布局与性能断言
// ---------------------------------------------------------
// 强制锁死 24 字节，完美契合 CPU 缓存行
static_assert(sizeof(Order) == 24, "Order size must be exactly 24 bytes for optimal cache performance");

// 确保允许底层通过寄存器或极致优化的 memcpy 瞬间搬运
static_assert(std::is_trivially_copyable_v<Order>, "Order must be trivially copyable");

} // namespace hft