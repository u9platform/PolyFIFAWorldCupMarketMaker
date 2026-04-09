#include "quote_engine.h"
#include <algorithm>
#include <cmath>

namespace mm {

int64_t QuoteEngine::toTicks(double price) {
    return static_cast<int64_t>(std::round(price * TICK_MULTIPLIER));
}

double QuoteEngine::fromTicks(int64_t ticks) {
    return static_cast<double>(ticks) * TICK_SIZE;
}

std::optional<Quote> QuoteEngine::calculateQuotes(double mid_price, double spread) {
    if (mid_price <= 0.0) {
        return std::nullopt;
    }

    double half = spread / 2.0;
    double raw_bid = mid_price - half;
    double raw_ask = mid_price + half;

    // Convert to ticks with epsilon to handle floating point (e.g. 0.021 * 1000 = 20.999...)
    constexpr double EPS = 1e-9;
    int64_t bid_ticks = static_cast<int64_t>(std::floor(raw_bid * TICK_MULTIPLIER + EPS));
    int64_t ask_ticks = static_cast<int64_t>(std::ceil(raw_ask * TICK_MULTIPLIER - EPS));

    // Enforce minimum 1 tick spread
    if (ask_ticks - bid_ticks < 1) {
        ask_ticks = bid_ticks + 1;
    }

    // Clamp to valid range
    bid_ticks = std::clamp(bid_ticks, MIN_PRICE_TICKS, MAX_PRICE_TICKS);
    ask_ticks = std::clamp(ask_ticks, MIN_PRICE_TICKS, MAX_PRICE_TICKS);

    return Quote{fromTicks(bid_ticks), fromTicks(ask_ticks)};
}

bool QuoteEngine::shouldRequote(double old_mid, double new_mid, double threshold) {
    return std::abs(new_mid - old_mid) > threshold;
}

} // namespace mm
