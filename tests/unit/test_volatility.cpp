#include "volatility_tracker.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(Volatility, EmptyWindow_ReturnsMinSigma) {
    VolatilityTracker vt(3600, 300000, 0.0001);
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.0001);
}

TEST(Volatility, SinglePrice_ReturnsMinSigma) {
    VolatilityTracker vt(3600, 300000, 0.0001);
    vt.addPrice(0.022, 1000);
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.0001);
}

TEST(Volatility, ConstantPrices_ReturnsMinSigma) {
    // Same price across multiple 5-min buckets → zero vol
    VolatilityTracker vt(3600, 300000, 0.0001);
    for (int i = 0; i < 10; i++) {
        vt.addPrice(0.022, static_cast<int64_t>(i) * 300000);  // every 5 min
    }
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.0001);
}

TEST(Volatility, AlternatingPrices_NonTrivialVol) {
    // Price alternates between 0.020 and 0.022 each 5-min bucket
    VolatilityTracker vt(3600, 300000, 0.0001);
    for (int i = 0; i < 10; i++) {
        double price = (i % 2 == 0) ? 0.020 : 0.022;
        vt.addPrice(price, static_cast<int64_t>(i) * 300000);
    }
    double s = vt.sigma();
    EXPECT_GT(s, 0.0001);
    // Changes are ±0.002, std dev should be ~0.002
    EXPECT_NEAR(s, 0.002, 0.001);
}

TEST(Volatility, WindowRolling_OldDropped) {
    // Window = 30 min = 1800s. Resample = 5 min = 300s
    VolatilityTracker vt(1800, 300000, 0.0001);

    // Add constant prices for first 30 min
    for (int i = 0; i < 6; i++) {
        vt.addPrice(0.022, static_cast<int64_t>(i) * 300000);
    }
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.0001);

    // Add volatile prices for next 30 min → old ones should drop
    for (int i = 6; i < 12; i++) {
        double price = (i % 2 == 0) ? 0.020 : 0.024;
        vt.addPrice(price, static_cast<int64_t>(i) * 300000);
    }
    EXPECT_GT(vt.sigma(), 0.0001);
}
