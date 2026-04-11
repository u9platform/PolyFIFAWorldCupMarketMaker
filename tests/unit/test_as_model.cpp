#include "quote_engine.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace mm;

static ASParams defaultParams() {
    return ASParams{
        .gamma = 0.1,
        .min_spread = 0.001,
        .k = 5.0,
        .time_to_expiry = 0.28,
        .max_inventory = 1000
    };
}

TEST(ASModel, ZeroInventory_ReservationEqualsFair) {
    auto q = QuoteEngine::calculateAS(0.022, 0, 0.5, defaultParams());
    ASSERT_TRUE(q.has_value());
    EXPECT_NEAR(q->reservation_price, 0.022, 1e-9);
}

TEST(ASModel, PositiveInventory_ReservationShiftsDown) {
    auto q = QuoteEngine::calculateAS(0.022, 100, 0.5, defaultParams());
    ASSERT_TRUE(q.has_value());
    EXPECT_LT(q->reservation_price, 0.022);
}

TEST(ASModel, NegativeInventory_ReservationShiftsUp) {
    auto q = QuoteEngine::calculateAS(0.022, -100, 0.5, defaultParams());
    ASSERT_TRUE(q.has_value());
    EXPECT_GT(q->reservation_price, 0.022);
}

TEST(ASModel, SpreadCalculation) {
    auto p = defaultParams();
    auto q = QuoteEngine::calculateAS(0.022, 0, 0.5, p);
    ASSERT_TRUE(q.has_value());

    // Expected: gamma * sigma^2 * tau + (2/gamma) * ln(1 + gamma/k)
    double expected_delta = p.gamma * 0.25 * 0.28 + (2.0 / p.gamma) * std::log(1.0 + p.gamma / p.k);
    EXPECT_NEAR(q->optimal_spread, expected_delta, 1e-9);
    EXPECT_GT(q->optimal_spread, 0.0);
}

TEST(ASModel, MinSpreadEnforced) {
    ASParams p = defaultParams();
    p.min_spread = 0.05;  // force large min
    p.gamma = 0.001;      // would produce tiny spread
    p.time_to_expiry = 0.001;

    auto q = QuoteEngine::calculateAS(0.5, 0, 0.01, p);
    ASSERT_TRUE(q.has_value());
    EXPECT_GE(q->ask_price - q->bid_price, 0.05 - 0.001);  // tick rounding tolerance
}

TEST(ASModel, TickAlignment) {
    auto q = QuoteEngine::calculateAS(0.022, 0, 0.5, defaultParams());
    ASSERT_TRUE(q.has_value());
    // Prices should be on tick grid
    EXPECT_NEAR(q->bid_price, std::round(q->bid_price * 1000) / 1000, 1e-9);
    EXPECT_NEAR(q->ask_price, std::round(q->ask_price * 1000) / 1000, 1e-9);
    EXPECT_GE(q->ask_price - q->bid_price, 0.001 - 1e-9);
}

TEST(ASModel, TimeDecay_ShorterExpiry_NarrowerSpread) {
    ASParams p_far = defaultParams();
    p_far.time_to_expiry = 0.28;
    ASParams p_near = defaultParams();
    p_near.time_to_expiry = 0.01;

    auto q_far = QuoteEngine::calculateAS(0.022, 0, 0.5, p_far);
    auto q_near = QuoteEngine::calculateAS(0.022, 0, 0.5, p_near);
    ASSERT_TRUE(q_far.has_value() && q_near.has_value());
    EXPECT_LT(q_near->optimal_spread, q_far->optimal_spread);
}

TEST(ASModel, InventoryCap_OnlyAsk) {
    ASParams p = defaultParams();
    p.max_inventory = 1000;
    auto q = QuoteEngine::calculateAS(0.022, 1500, 0.5, p);
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(q->bid_price, 0.0);  // no bid (already too long)
    EXPECT_GT(q->ask_price, 0.0);  // ask still active
}

TEST(ASModel, InventoryCap_OnlyBid) {
    ASParams p = defaultParams();
    p.max_inventory = 1000;
    auto q = QuoteEngine::calculateAS(0.022, -1500, 0.5, p);
    ASSERT_TRUE(q.has_value());
    EXPECT_GT(q->bid_price, 0.0);  // bid still active
    EXPECT_EQ(q->ask_price, 0.0);  // no ask (already too short)
}

TEST(ASModel, InventoryWithinCap_BothSides) {
    ASParams p = defaultParams();
    p.max_inventory = 1000;
    auto q = QuoteEngine::calculateAS(0.022, 500, 0.5, p);
    ASSERT_TRUE(q.has_value());
    EXPECT_GT(q->bid_price, 0.0);
    EXPECT_GT(q->ask_price, 0.0);
}
