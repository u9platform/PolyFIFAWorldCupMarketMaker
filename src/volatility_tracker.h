#pragma once

#include <deque>
#include <cstddef>

namespace mm {

class VolatilityTracker {
public:
    explicit VolatilityTracker(size_t window_size = 100, double min_sigma = 0.01);

    void addPrice(double price);

    // Annualized realized volatility.
    double sigma() const;

    size_t sampleCount() const { return prices_.size(); }

private:
    size_t window_size_;
    double min_sigma_;
    std::deque<double> prices_;
};

} // namespace mm
