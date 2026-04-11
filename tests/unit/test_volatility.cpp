#include "volatility_tracker.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace mm;

TEST(Volatility, EmptyWindow_ReturnsMinSigma) {
    VolatilityTracker vt(100, 0.01);
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.01);
}

TEST(Volatility, SinglePrice_ReturnsMinSigma) {
    VolatilityTracker vt(100, 0.01);
    vt.addPrice(0.022);
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.01);
}

TEST(Volatility, ConstantPrices_ReturnsMinSigma) {
    VolatilityTracker vt(100, 0.01);
    for (int i = 0; i < 50; i++) vt.addPrice(0.022);
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.01);
}

TEST(Volatility, AlternatingPrices_NonTrivialVol) {
    VolatilityTracker vt(100, 0.01);
    for (int i = 0; i < 50; i++) {
        vt.addPrice(i % 2 == 0 ? 0.020 : 0.022);
    }
    EXPECT_GT(vt.sigma(), 0.01);
}

TEST(Volatility, WindowRolling_OldDropped) {
    VolatilityTracker vt(5, 0.01);
    // Add 5 constant prices
    for (int i = 0; i < 5; i++) vt.addPrice(0.022);
    EXPECT_DOUBLE_EQ(vt.sigma(), 0.01);  // constant = min

    // Now add 5 alternating prices, old constant ones drop out
    for (int i = 0; i < 5; i++) {
        vt.addPrice(i % 2 == 0 ? 0.020 : 0.024);
    }
    EXPECT_GT(vt.sigma(), 0.01);
    EXPECT_EQ(vt.sampleCount(), 5u);
}
