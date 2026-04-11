#pragma once

#include "types.h"
#include <optional>

namespace mm {

struct Quote {
    double bid_price;
    double ask_price;
};

struct ASParams {
    double gamma = 0.1;          // risk aversion
    double min_spread = 0.001;   // minimum spread (1 tick)
    double k = 5.0;              // order arrival intensity
    double time_to_expiry = 0.28; // years until expiry
    double max_inventory = 1000;  // hard inventory cap (shares)
};

struct ASQuote {
    double bid_price = 0;        // 0 means "don't place this side"
    double ask_price = 0;
    double reservation_price;
    double optimal_spread;
};

class QuoteEngine {
public:
    // Original fixed-spread calculation (backward compat).
    static std::optional<Quote> calculateQuotes(double mid_price, double spread);

    // Avellaneda-Stoikov model.
    // fair_value: from FairValueCalculator (default = mid)
    // inventory: current position in shares (positive = long)
    // sigma: annualized volatility
    // Returns ASQuote with bid/ask (0 = don't quote that side due to inventory cap).
    static std::optional<ASQuote> calculateAS(double fair_value, double inventory,
                                               double sigma, const ASParams& params);

    static bool shouldRequote(double old_mid, double new_mid, double threshold);

    static int64_t toTicks(double price);
    static double fromTicks(int64_t ticks);
};

} // namespace mm
