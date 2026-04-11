#pragma once

namespace mm {

struct FairValueParams {
    double ext_weight = 0.7;       // weight for external price
    double max_deviation = 0.03;   // max allowed |mid - ext|
};

class FairValueCalculator {
public:
    explicit FairValueCalculator(const FairValueParams& params = {});

    // Default mode: returns mid_price.
    double calculate(double mid_price) const;

    // External weighted mode: blends mid with external.
    // If has_external is false, falls back to mid_price.
    double calculate(double mid_price, double external_price, bool has_external) const;

private:
    FairValueParams params_;
};

} // namespace mm
