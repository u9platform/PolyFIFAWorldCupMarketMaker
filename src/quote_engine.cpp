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

std::optional<ASQuote> QuoteEngine::calculateAS(double fair_value, double inventory,
                                                  double sigma, const ASParams& params) {
    if (fair_value <= 0.0) return std::nullopt;

    double gamma = params.gamma;
    double tau = params.time_to_expiry;  // T - t in years
    double k = params.k;

    // Reservation price: r = s - q * γ * σ² * τ
    double r = fair_value - inventory * gamma * sigma * sigma * tau;

    // Optimal spread: δ = γ * σ² * τ + (2/γ) * ln(1 + γ/k)
    double delta = gamma * sigma * sigma * tau + (2.0 / gamma) * std::log(1.0 + gamma / k);

    // Enforce minimum spread
    delta = std::max(delta, params.min_spread);

    double raw_bid = r - delta / 2.0;
    double raw_ask = r + delta / 2.0;

    // Tick alignment
    constexpr double EPS = 1e-9;
    int64_t bid_ticks = static_cast<int64_t>(std::floor(raw_bid * TICK_MULTIPLIER + EPS));
    int64_t ask_ticks = static_cast<int64_t>(std::ceil(raw_ask * TICK_MULTIPLIER - EPS));

    if (ask_ticks - bid_ticks < 1) {
        ask_ticks = bid_ticks + 1;
    }

    bid_ticks = std::clamp(bid_ticks, MIN_PRICE_TICKS, MAX_PRICE_TICKS);
    ask_ticks = std::clamp(ask_ticks, MIN_PRICE_TICKS, MAX_PRICE_TICKS);

    ASQuote q;
    q.reservation_price = r;
    q.optimal_spread = delta;
    q.bid_price = fromTicks(bid_ticks);
    q.ask_price = fromTicks(ask_ticks);

    // Inventory hard cap: suppress one side if over limit
    if (inventory > params.max_inventory) {
        q.bid_price = 0;  // don't buy more
    } else if (inventory < -params.max_inventory) {
        q.ask_price = 0;  // don't sell more
    }

    return q;
}

bool QuoteEngine::shouldRequote(double old_mid, double new_mid, double threshold) {
    return std::abs(new_mid - old_mid) > threshold;
}

} // namespace mm
