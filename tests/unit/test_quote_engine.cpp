#include "quote_engine.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace mm;

TEST(QuoteEngine, CalculateBidAndAsk) {
    auto q = QuoteEngine::calculateQuotes(0.022, 0.002);
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->bid_price, 0.021);
    EXPECT_DOUBLE_EQ(q->ask_price, 0.023);
}

TEST(QuoteEngine, PriceAlignToTick_FloorCeil) {
    // mid = 0.02237, spread = 0.002
    // raw_bid = 0.02137 -> floor(21.37) = 21 -> 0.021
    // raw_ask = 0.02337 -> ceil(23.37)  = 24 -> 0.024
    auto q = QuoteEngine::calculateQuotes(0.02237, 0.002);
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->bid_price, 0.021);
    EXPECT_DOUBLE_EQ(q->ask_price, 0.024);
}

TEST(QuoteEngine, SpreadSmallerThanTick_EnforceMinSpread) {
    auto q = QuoteEngine::calculateQuotes(0.022, 0.0005);
    ASSERT_TRUE(q.has_value());
    // Key assertion: at least 1 tick spread
    EXPECT_GE(q->ask_price - q->bid_price, 0.001 - 1e-9);
}

TEST(QuoteEngine, MidIsZero_ReturnsEmpty) {
    auto q = QuoteEngine::calculateQuotes(0.0, 0.002);
    EXPECT_FALSE(q.has_value());
}

TEST(QuoteEngine, BidClampToMinimum) {
    // mid = 0.001, spread = 0.004
    // raw_bid = 0.001 - 0.002 = -0.001 -> clamp to 1 tick = 0.001
    // raw_ask = 0.001 + 0.002 = 0.003
    auto q = QuoteEngine::calculateQuotes(0.001, 0.004);
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->bid_price, 0.001);
    EXPECT_DOUBLE_EQ(q->ask_price, 0.003);
}

TEST(QuoteEngine, AskClampToMaximum) {
    // mid = 0.998, spread = 0.004
    // raw_bid = 0.998 - 0.002 = 0.996
    // raw_ask = 0.998 + 0.002 = 1.000 -> clamp to 0.999
    auto q = QuoteEngine::calculateQuotes(0.998, 0.004);
    ASSERT_TRUE(q.has_value());
    EXPECT_DOUBLE_EQ(q->bid_price, 0.996);
    EXPECT_DOUBLE_EQ(q->ask_price, 0.999);
}

TEST(QuoteEngine, ShouldRequote_ChangeAboveThreshold) {
    EXPECT_TRUE(QuoteEngine::shouldRequote(0.022, 0.024, 0.001));
}

TEST(QuoteEngine, ShouldRequote_ChangeBelowThreshold) {
    EXPECT_FALSE(QuoteEngine::shouldRequote(0.022, 0.0225, 0.001));
}

TEST(QuoteEngine, TickConversion) {
    EXPECT_EQ(QuoteEngine::toTicks(0.021), 21);
    EXPECT_DOUBLE_EQ(QuoteEngine::fromTicks(21), 0.021);

    // Floating point safety: 0.001 * 3 should equal 3 ticks
    EXPECT_EQ(QuoteEngine::toTicks(0.001 * 3), 3);
    EXPECT_EQ(QuoteEngine::toTicks(0.1 + 0.2), QuoteEngine::toTicks(0.3));
}
