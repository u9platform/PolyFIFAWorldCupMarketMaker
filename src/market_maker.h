#pragma once

#include "api_client.h"
#include "config.h"
#include "order_book.h"
#include "order_manager.h"
#include "pnl_reporter.h"
#include "position_tracker.h"
#include "quote_engine.h"
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

struct MarketState {
    std::string token_id;
    double last_mid = 0.0;
    std::string active_bid_id;
    std::string active_ask_id;
    PositionTracker position;

    // PnlReporter owns a reference to position, constructed after position is stable.
    std::unique_ptr<PnlReporter> reporter;

    explicit MarketState(const std::string& tid) : token_id(tid) {
        reporter = std::make_unique<PnlReporter>(position);
    }

    // Move only
    MarketState(MarketState&&) = default;
    MarketState& operator=(MarketState&&) = default;
};

class MarketMaker {
public:
    MarketMaker(const Config& config, IApiClient& api);

    void start();
    void stop();

    // Run one iteration for all markets (for testing).
    void tick();

    // Run one iteration for a specific market (for testing).
    void tickMarket(const std::string& token_id);

    // Accessors
    const PositionTracker& positionTracker() const;  // first market, backward compat
    const PositionTracker& positionTracker(const std::string& token_id) const;
    const OrderManager& orderManager() const { return order_manager_; }
    double lastMid() const;  // first market
    double lastMid(const std::string& token_id) const;

    // Portfolio-level
    double portfolioExposure() const;
    size_t marketCount() const { return markets_.size(); }

private:
    void tickSingleMarket(MarketState& ms);
    void placeBothSides(MarketState& ms, const Quote& quote);

    Config config_;
    IApiClient& api_;
    OrderManager order_manager_;

    // Per-market state. Using vector for cache-friendly iteration + map for lookup.
    std::vector<MarketState> markets_;
    std::unordered_map<std::string, size_t> market_index_;  // token_id -> index into markets_

    bool balance_checked_ = false;
    std::atomic<bool> running_{false};
};

} // namespace mm
