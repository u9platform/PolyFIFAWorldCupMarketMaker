#pragma once

#include "api_client.h"
#include "config.h"
#include "order_book.h"
#include "order_manager.h"
#include "pnl_reporter.h"
#include "position_tracker.h"
#include "quote_engine.h"
#include <atomic>

namespace mm {

class MarketMaker {
public:
    MarketMaker(const Config& config, IApiClient& api);

    // Run the main loop. Blocks until stop() is called.
    void start();

    // Signal the main loop to stop.
    void stop();

    // Run one iteration of the main loop (for testing).
    void tick();

    // Accessors for testing.
    const PositionTracker& positionTracker() const { return position_tracker_; }
    const OrderManager& orderManager() const { return order_manager_; }
    double lastMid() const { return last_mid_; }

private:
    void handleFills();
    void updateQuotes();
    void placeBothSides(const Quote& quote);

    Config config_;
    IApiClient& api_;
    OrderManager order_manager_;
    PositionTracker position_tracker_;
    PnlReporter pnl_reporter_;

    double last_mid_ = 0.0;
    std::string active_bid_id_;
    std::string active_ask_id_;
    std::atomic<bool> running_{false};
};

} // namespace mm
