#include "fair_value.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(FairValue, DefaultMode_EqualsMid) {
    FairValueCalculator fv;
    EXPECT_DOUBLE_EQ(fv.calculate(0.022), 0.022);
}

TEST(FairValue, ExternalWeighted) {
    FairValueParams p{.ext_weight = 0.7, .max_deviation = 0.03};
    FairValueCalculator fv(p);
    // 0.3 * 0.020 + 0.7 * 0.024 = 0.006 + 0.0168 = 0.0228
    EXPECT_NEAR(fv.calculate(0.020, 0.024, true), 0.0228, 1e-9);
}

TEST(FairValue, ExternalUnavailable_FallbackToMid) {
    FairValueParams p{.ext_weight = 0.7};
    FairValueCalculator fv(p);
    EXPECT_DOUBLE_EQ(fv.calculate(0.022, 0.0, false), 0.022);
}

TEST(FairValue, DeviationWithinLimit_NormalWeight) {
    FairValueParams p{.ext_weight = 0.7, .max_deviation = 0.03};
    FairValueCalculator fv(p);
    // deviation = |0.015 - 0.025| = 0.01 < 0.03
    double result = fv.calculate(0.015, 0.025, true);
    EXPECT_NEAR(result, 0.3 * 0.015 + 0.7 * 0.025, 1e-9);
}

TEST(FairValue, DeviationExceedsLimit_LeanTowardExternal) {
    FairValueParams p{.ext_weight = 0.7, .max_deviation = 0.03};
    FairValueCalculator fv(p);
    // deviation = |0.010 - 0.050| = 0.04 > 0.03 → ext weight bumped to 0.9
    double result = fv.calculate(0.010, 0.050, true);
    EXPECT_NEAR(result, 0.1 * 0.010 + 0.9 * 0.050, 1e-9);
}
