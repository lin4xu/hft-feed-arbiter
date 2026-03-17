#pragma once
#include <cstdint>
#include <type_traits>

namespace hft {

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

struct Order {
    uint32_t order_id;
    uint32_t price;
    uint32_t quantity;
    Side side;
    uint64_t timestamp;

    Order() = default;

    Order(uint32_t id, uint32_t p, uint32_t q, Side s, uint64_t ts = 0)
        : order_id(id), price(p), quantity(q), side(s), timestamp(ts) {}
};

// 强制锁死 24 字节
static_assert(sizeof(Order) == 24, "Order size must be exactly 24 bytes for optimal cache performance");

static_assert(std::is_trivially_copyable_v<Order>, "Order must be trivially copyable");

}