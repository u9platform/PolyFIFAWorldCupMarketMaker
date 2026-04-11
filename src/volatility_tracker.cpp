#include "volatility_tracker.h"
#include <cmath>
#include <numeric>

namespace mm {

VolatilityTracker::VolatilityTracker(size_t window_size, double min_sigma)
    : window_size_(window_size), min_sigma_(min_sigma) {}

void VolatilityTracker::addPrice(double price) {
    if (price <= 0) return;
    prices_.push_back(price);
    if (prices_.size() > window_size_) {
        prices_.pop_front();
    }
}

double VolatilityTracker::sigma() const {
    if (prices_.size() < 2) return min_sigma_;

    // Compute log returns
    std::vector<double> returns;
    returns.reserve(prices_.size() - 1);
    for (size_t i = 1; i < prices_.size(); ++i) {
        if (prices_[i - 1] > 0 && prices_[i] > 0) {
            returns.push_back(std::log(prices_[i] / prices_[i - 1]));
        }
    }

    if (returns.empty()) return min_sigma_;

    // Mean
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();

    // Variance
    double var = 0;
    for (double r : returns) {
        double d = r - mean;
        var += d * d;
    }
    var /= returns.size();

    double std_dev = std::sqrt(var);

    // Annualize: assume each sample is ~5 seconds apart (poll interval),
    // so samples_per_year ≈ 365.25 * 24 * 3600 / 5 ≈ 6,311,520
    // But this gives absurdly high annualized vol for low-freq data.
    // Better: just return the per-sample std_dev, let gamma handle scaling.
    // The AS formula uses σ² * (T-t) where T-t is in years,
    // so σ should be annualized. We use sqrt(samples_per_year) scaling.
    constexpr double SAMPLES_PER_YEAR = 365.25 * 24 * 3600 / 5.0;
    double annualized = std_dev * std::sqrt(SAMPLES_PER_YEAR);

    return std::max(annualized, min_sigma_);
}

} // namespace mm
