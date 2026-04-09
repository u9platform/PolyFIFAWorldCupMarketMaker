#include "pnl_reporter.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(PnlReporter, NoTrades) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    auto r = reporter.generateReport(0.022);
    EXPECT_EQ(r.total_trades, 0);
    EXPECT_DOUBLE_EQ(r.realized_pnl, 0.0);
    EXPECT_DOUBLE_EQ(r.avg_spread_earned, 0.0);
}

TEST(PnlReporter, OneCompleteRoundTrip) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    reporter.recordFill(Side::BUY, 0.021, 100, 1000);
    reporter.recordFill(Side::SELL, 0.023, 100, 2000);
    auto r = reporter.generateReport(0.022);
    EXPECT_EQ(r.total_trades, 2);
    EXPECT_NEAR(r.realized_pnl, 0.2, 1e-9);
    EXPECT_NEAR(r.avg_spread_earned, 0.002, 1e-9);
}

TEST(PnlReporter, MultipleTradesStats) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    // 5 round trips over 5 hours
    for (int i = 0; i < 5; ++i) {
        int64_t ts_base = i * 3600000;  // 1 hour apart
        reporter.recordFill(Side::BUY, 0.021, 100, ts_base);
        reporter.recordFill(Side::SELL, 0.023, 100, ts_base + 1000);
    }
    auto r = reporter.generateReport(0.022);
    EXPECT_EQ(r.total_trades, 10);
    EXPECT_NEAR(r.realized_pnl, 1.0, 1e-9);
    EXPECT_NEAR(r.avg_spread_earned, 0.002, 1e-9);
    // trades_per_hour: 10 trades over ~4 hours span
    EXPECT_GT(r.trades_per_hour, 0.0);
}

TEST(PnlReporter, LossRoundTrip) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    reporter.recordFill(Side::BUY, 0.023, 100, 1000);
    reporter.recordFill(Side::SELL, 0.021, 100, 2000);
    auto r = reporter.generateReport(0.022);
    EXPECT_NEAR(r.realized_pnl, -0.2, 1e-9);
}

TEST(PnlReporter, AsymmetricQuantities) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    reporter.recordFill(Side::BUY, 0.021, 100, 1000);
    reporter.recordFill(Side::SELL, 0.023, 50, 2000);
    reporter.recordFill(Side::SELL, 0.022, 50, 3000);
    auto r = reporter.generateReport(0.022);
    EXPECT_EQ(r.total_trades, 3);
    // pnl = 50*(0.023-0.021) + 50*(0.022-0.021) = 0.1 + 0.05 = 0.15
    EXPECT_NEAR(r.realized_pnl, 0.15, 1e-9);
}

TEST(PnlReporter, TradeRecordDetail) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    reporter.recordFill(Side::BUY, 0.021, 100, 1000);
    auto trades = reporter.getTrades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].side, Side::BUY);
    EXPECT_DOUBLE_EQ(trades[0].price, 0.021);
    EXPECT_DOUBLE_EQ(trades[0].qty, 100.0);
    EXPECT_EQ(trades[0].timestamp_ms, 1000);
}

TEST(PnlReporter, TotalPnlEqualsRealizedPlusUnrealized) {
    PositionTracker pt;
    PnlReporter reporter(pt);
    reporter.recordFill(Side::BUY, 0.021, 100, 1000);
    auto r = reporter.generateReport(0.025);
    EXPECT_NEAR(r.realized_pnl, 0.0, 1e-9);
    EXPECT_NEAR(r.unrealized_pnl, 0.4, 1e-9);
    EXPECT_NEAR(r.total_pnl, 0.4, 1e-9);
}
