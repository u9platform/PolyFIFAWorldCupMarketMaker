#include "position_tracker.h"
#include <cmath>

namespace mm {

void PositionTracker::onFill(Side side, double price, double qty) {
    trades_.push_back(Trade{side, price, qty, nowMs()});

    if (side == Side::BUY) {
        if (yes_position_ >= 0) {
            // Adding to long or opening long from flat
            double old_cost = avg_cost_ * yes_position_;
            double new_cost = price * qty;
            yes_position_ += qty;
            avg_cost_ = (old_cost + new_cost) / yes_position_;
        } else {
            // Covering short position
            double cover_qty = std::min(qty, -yes_position_);
            realized_pnl_ += cover_qty * (avg_cost_ - price);

            double remaining = qty - cover_qty;
            yes_position_ += qty;

            if (yes_position_ > 0) {
                // Flipped to long
                avg_cost_ = price;
            } else if (yes_position_ == 0) {
                avg_cost_ = 0.0;
            }
            // Still short: avg_cost unchanged
        }
    } else {
        // SELL
        if (yes_position_ <= 0) {
            // Adding to short or opening short from flat
            double old_cost = std::abs(avg_cost_ * yes_position_);
            double new_cost = price * qty;
            yes_position_ -= qty;
            avg_cost_ = (old_cost + new_cost) / std::abs(yes_position_);
        } else {
            // Closing long position
            double close_qty = std::min(qty, yes_position_);
            realized_pnl_ += close_qty * (price - avg_cost_);

            double remaining = qty - close_qty;
            yes_position_ -= qty;

            if (yes_position_ < 0) {
                // Flipped to short
                avg_cost_ = price;
            } else if (yes_position_ == 0) {
                avg_cost_ = 0.0;
            }
            // Still long: avg_cost unchanged
        }
    }
}

double PositionTracker::unrealizedPnl(double mark_price) const {
    if (yes_position_ > 0) {
        return yes_position_ * (mark_price - avg_cost_);
    } else if (yes_position_ < 0) {
        return std::abs(yes_position_) * (avg_cost_ - mark_price);
    }
    return 0.0;
}

double PositionTracker::totalPnl(double mark_price) const {
    return realized_pnl_ + unrealizedPnl(mark_price);
}

} // namespace mm
