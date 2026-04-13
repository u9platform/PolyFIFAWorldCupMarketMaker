#include "volatility_tracker.h"
#include "types.h"
#include <cmath>
#include <numeric>
#include <vector>
#include <map>

namespace mm {

VolatilityTracker::VolatilityTracker(int window_seconds, int resample_ms, double min_sigma)
    : window_seconds_(window_seconds), resample_ms_(resample_ms), min_sigma_(min_sigma) {}

void VolatilityTracker::addPrice(double price) {
    addPrice(price, nowMs());
}

void VolatilityTracker::addPrice(double price, int64_t timestamp_ms) {
    if (price <= 0) return;
    raw_.push_back({timestamp_ms, price});

    // Trim old samples outside window
    int64_t cutoff = timestamp_ms - static_cast<int64_t>(window_seconds_) * 1000;
    while (!raw_.empty() && raw_.front().ts < cutoff) {
        raw_.pop_front();
    }
}

double VolatilityTracker::sigma() const {
    if (raw_.size() < 2) return min_sigma_;

    // Resample: take last price in each bucket
    std::map<int64_t, double> buckets;
    for (auto& s : raw_) {
        int64_t bucket = s.ts / resample_ms_;
        buckets[bucket] = s.price;  // last price wins
    }

    if (buckets.size() < 2) return min_sigma_;

    // Compute absolute price changes between consecutive buckets
    std::vector<double> changes;
    auto it = buckets.begin();
    double prev_price = it->second;
    ++it;
    for (; it != buckets.end(); ++it) {
        changes.push_back(it->second - prev_price);
        prev_price = it->second;
    }

    if (changes.empty()) return min_sigma_;

    // Standard deviation of absolute changes
    double mean = std::accumulate(changes.begin(), changes.end(), 0.0) / changes.size();
    double var = 0;
    for (double c : changes) {
        double d = c - mean;
        var += d * d;
    }
    var /= changes.size();

    return std::max(std::sqrt(var), min_sigma_);
}

} // namespace mm
