#pragma once

#include "types.h"
#include <optional>

namespace mm {

struct Quote {
    double bid_price;
    double ask_price;
};

class QuoteEngine {
public:
    // Calculate bid and ask prices from mid and spread.
    // Returns nullopt if mid_price <= 0.
    // Prices are aligned to tick, clamped to [0.001, 0.999].
    // Guarantees ask - bid >= 1 tick.
    static std::optional<Quote> calculateQuotes(double mid_price, double spread);

    // Returns true if |old_mid - new_mid| > threshold.
    static bool shouldRequote(double old_mid, double new_mid, double threshold);

    // Tick conversion helpers exposed for testing.
    static int64_t toTicks(double price);
    static double fromTicks(int64_t ticks);
};

} // namespace mm
