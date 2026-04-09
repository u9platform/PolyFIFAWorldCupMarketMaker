#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>

namespace mm {

// Tick arithmetic: all prices internally represented as integer ticks
// 1 tick = 0.001, so 0.021 = 21 ticks
static constexpr double TICK_SIZE = 0.001;
static constexpr int64_t TICK_MULTIPLIER = 1000;
static constexpr int64_t MIN_PRICE_TICKS = 1;    // 0.001
static constexpr int64_t MAX_PRICE_TICKS = 999;  // 0.999

inline int64_t toTicks(double price) {
    return static_cast<int64_t>(std::round(price * TICK_MULTIPLIER));
}

inline double fromTicks(int64_t ticks) {
    return static_cast<double>(ticks) * TICK_SIZE;
}

enum class Side { BUY, SELL };

struct Fill {
    Side side;
    double price;
    double qty;
    int64_t timestamp_ms;  // epoch millis
};

struct Trade {
    Side side;
    double price;
    double qty;
    int64_t timestamp_ms;
};

struct OrderInfo {
    std::string order_id;
    Side side;
    double price;
    double qty;
    double filled_qty = 0.0;
};

enum class OrderStatus {
    LIVE,
    FILLED,
    PARTIALLY_FILLED,
    CANCELED,
    UNKNOWN
};

inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace mm
