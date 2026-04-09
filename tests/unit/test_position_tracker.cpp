#include "position_tracker.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(PositionTracker, InitialState) {
    PositionTracker pt;
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 0.0);
    EXPECT_DOUBLE_EQ(pt.avgCost(), 0.0);
    EXPECT_DOUBLE_EQ(pt.realizedPnl(), 0.0);
}

TEST(PositionTracker, BuyRecord) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 100.0);
    EXPECT_DOUBLE_EQ(pt.avgCost(), 0.021);
}

TEST(PositionTracker, SellRecord_FullClose) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);
    pt.onFill(Side::SELL, 0.023, 100);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 0.0);
    EXPECT_NEAR(pt.realizedPnl(), 0.2, 1e-9);
}

TEST(PositionTracker, PartialSell) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);
    pt.onFill(Side::SELL, 0.023, 50);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 50.0);
    EXPECT_DOUBLE_EQ(pt.avgCost(), 0.021);
    EXPECT_NEAR(pt.realizedPnl(), 0.1, 1e-9);
}

TEST(PositionTracker, MultipleBuys_AverageCost) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);
    pt.onFill(Side::BUY, 0.023, 100);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 200.0);
    EXPECT_NEAR(pt.avgCost(), 0.022, 1e-9);
}

TEST(PositionTracker, UnrealizedPnl_Profit) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);
    EXPECT_NEAR(pt.unrealizedPnl(0.025), 0.4, 1e-9);
}

TEST(PositionTracker, UnrealizedPnl_Loss) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);
    EXPECT_NEAR(pt.unrealizedPnl(0.019), -0.2, 1e-9);
}

TEST(PositionTracker, ShortOpen) {
    PositionTracker pt;
    pt.onFill(Side::SELL, 0.023, 100);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), -100.0);
    EXPECT_DOUBLE_EQ(pt.avgCost(), 0.023);
}

TEST(PositionTracker, ShortClose_Profit) {
    PositionTracker pt;
    pt.onFill(Side::SELL, 0.023, 100);
    pt.onFill(Side::BUY, 0.021, 100);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 0.0);
    EXPECT_NEAR(pt.realizedPnl(), 0.2, 1e-9);
}

TEST(PositionTracker, ShortClose_Loss) {
    PositionTracker pt;
    pt.onFill(Side::SELL, 0.021, 100);
    pt.onFill(Side::BUY, 0.023, 100);
    EXPECT_DOUBLE_EQ(pt.yesPosition(), 0.0);
    EXPECT_NEAR(pt.realizedPnl(), -0.2, 1e-9);
}

TEST(PositionTracker, MixedOperations) {
    PositionTracker pt;
    pt.onFill(Side::BUY, 0.021, 100);   // pos=100, avg=0.021
    pt.onFill(Side::SELL, 0.023, 50);    // pos=50,  rpnl = 50*(0.023-0.021) = 0.1
    pt.onFill(Side::BUY, 0.020, 50);     // pos=100, avg = (50*0.021 + 50*0.020)/100 = 0.0205
    pt.onFill(Side::SELL, 0.022, 100);   // pos=0,   rpnl += 100*(0.022-0.0205) = 0.15

    EXPECT_DOUBLE_EQ(pt.yesPosition(), 0.0);
    EXPECT_NEAR(pt.realizedPnl(), 0.25, 1e-9);
}
