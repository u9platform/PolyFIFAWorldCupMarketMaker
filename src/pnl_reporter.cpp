#include "pnl_reporter.h"
#include <numeric>
#include <cmath>

namespace mm {

PnlReporter::PnlReporter(PositionTracker& tracker) : tracker_(tracker) {}

void PnlReporter::recordFill(Side side, double price, double qty, int64_t timestamp_ms) {
    tracker_.onFill(side, price, qty);
    trades_.push_back(Trade{side, price, qty, timestamp_ms});
    if (first_trade_ts_ == 0) {
        first_trade_ts_ = timestamp_ms;
    }
}

PnlReport PnlReporter::generateReport(double mark_price) const {
    PnlReport r{};
    r.total_trades = static_cast<int>(trades_.size());
    r.realized_pnl = tracker_.realizedPnl();
    r.unrealized_pnl = tracker_.unrealizedPnl(mark_price);
    r.total_pnl = r.realized_pnl + r.unrealized_pnl;

    // Total volume
    r.total_volume = 0;
    for (auto& t : trades_) {
        r.total_volume += t.price * t.qty;
    }

    // Average spread earned: realized PnL / (number of round-trip pairs)
    // A round trip = 1 buy + 1 sell. Count pairs as min(buys, sells).
    int buys = 0, sells = 0;
    double buy_qty = 0, sell_qty = 0;
    for (auto& t : trades_) {
        if (t.side == Side::BUY) {
            buys++;
            buy_qty += t.qty;
        } else {
            sells++;
            sell_qty += t.qty;
        }
    }
    double matched_qty = std::min(buy_qty, sell_qty);
    r.avg_spread_earned = matched_qty > 0 ? r.realized_pnl / matched_qty : 0.0;

    // Trades per hour
    if (r.total_trades >= 2 && trades_.back().timestamp_ms > first_trade_ts_) {
        double hours = (trades_.back().timestamp_ms - first_trade_ts_) / 3600000.0;
        r.trades_per_hour = hours > 0 ? r.total_trades / hours : 0.0;
    } else {
        r.trades_per_hour = 0.0;
    }

    return r;
}

const std::vector<Trade>& PnlReporter::getTrades() const {
    return trades_;
}

} // namespace mm
