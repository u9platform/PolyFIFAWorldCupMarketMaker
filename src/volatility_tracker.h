#pragma once

#include <deque>
#include <cstddef>
#include <cstdint>

namespace mm {

class VolatilityTracker {
public:
    // window_seconds: how far back to look (default 1 hour)
    // resample_ms: bucket size for resampling (default 5 minutes)
    // min_sigma: floor value
    explicit VolatilityTracker(int window_seconds = 3600,
                               int resample_ms = 300000,
                               double min_sigma = 0.0001);

    // Old interface (backward compat for tests) - uses wall clock
    void addPrice(double price);

    // Preferred: with explicit timestamp
    void addPrice(double price, int64_t timestamp_ms);

    // Volatility of absolute price changes over resampled intervals.
    // NOT annualized. Gamma absorbs the scaling.
    double sigma() const;

    size_t sampleCount() const { return raw_.size(); }

private:
    struct Sample { int64_t ts; double price; };

    int window_seconds_;
    int resample_ms_;
    double min_sigma_;
    std::deque<Sample> raw_;
};

} // namespace mm
