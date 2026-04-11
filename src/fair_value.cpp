#include "fair_value.h"
#include <cmath>

namespace mm {

FairValueCalculator::FairValueCalculator(const FairValueParams& params)
    : params_(params) {}

double FairValueCalculator::calculate(double mid_price) const {
    return mid_price;
}

double FairValueCalculator::calculate(double mid_price, double external_price,
                                       bool has_external) const {
    if (!has_external) {
        return mid_price;
    }

    double w_ext = params_.ext_weight;
    double deviation = std::abs(mid_price - external_price);

    // If deviation exceeds max, lean harder toward external
    if (deviation > params_.max_deviation) {
        w_ext = 0.9;  // trust external more when big divergence
    }

    double w_mid = 1.0 - w_ext;
    return w_mid * mid_price + w_ext * external_price;
}

} // namespace mm
