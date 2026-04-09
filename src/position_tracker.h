#pragma once

#include "types.h"
#include <vector>

namespace mm {

class PositionTracker {
public:
    void onFill(Side side, double price, double qty);

    double yesPosition() const { return yes_position_; }
    double avgCost() const { return avg_cost_; }
    double realizedPnl() const { return realized_pnl_; }
    double unrealizedPnl(double mark_price) const;
    double totalPnl(double mark_price) const;

    const std::vector<Trade>& trades() const { return trades_; }

private:
    double yes_position_ = 0.0;
    double avg_cost_ = 0.0;
    double realized_pnl_ = 0.0;
    std::vector<Trade> trades_;
};

} // namespace mm
