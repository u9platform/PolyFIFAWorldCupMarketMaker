#pragma once

#include "position_tracker.h"
#include <string>

namespace mm {

struct PnlReport {
    int total_trades;
    double realized_pnl;
    double unrealized_pnl;
    double total_pnl;
    double avg_spread_earned;
    double total_volume;       // sum of all trade notionals
    double trades_per_hour;
};

class PnlReporter {
public:
    explicit PnlReporter(PositionTracker& tracker);

    void recordFill(Side side, double price, double qty, int64_t timestamp_ms);
    PnlReport generateReport(double mark_price) const;
    const std::vector<Trade>& getTrades() const;

private:
    PositionTracker& tracker_;
    std::vector<Trade> trades_;
    int64_t first_trade_ts_ = 0;
};

} // namespace mm
