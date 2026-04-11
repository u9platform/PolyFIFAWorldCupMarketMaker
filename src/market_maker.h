#pragma once

#include "api_client.h"
#include "config.h"
#include "fair_value.h"
#include "order_book.h"
#include "order_manager.h"
#include "pnl_reporter.h"
#include "position_tracker.h"
#include "quote_engine.h"
#include "volatility_tracker.h"
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

struct MarketState {
    std::string token_id;
    double last_reservation = 0.0;  // last reservation price (for requote check)
    std::string active_bid_id;
    std::string active_ask_id;
    PositionTracker position;
    VolatilityTracker vol_tracker;

    std::unique_ptr<PnlReporter> reporter;

    explicit MarketState(const std::string& tid, size_t vol_window = 100)
        : token_id(tid), vol_tracker(vol_window) {
        reporter = std::make_unique<PnlReporter>(position);
    }

    MarketState(MarketState&&) = default;
    MarketState& operator=(MarketState&&) = default;
};

class MarketMaker {
public:
    MarketMaker(const Config& config, IApiClient& api);

    void start();
    void stop();

    void tick();
    void tickMarket(const std::string& token_id);

    // Accessors
    const PositionTracker& positionTracker() const;
    const PositionTracker& positionTracker(const std::string& token_id) const;
    const OrderManager& orderManager() const { return order_manager_; }
    double lastMid() const;
    double lastMid(const std::string& token_id) const;

    double portfolioExposure() const;
    size_t marketCount() const { return markets_.size(); }

private:
    void tickSingleMarket(MarketState& ms);
    void placeASQuote(MarketState& ms, const ASQuote& quote);

    Config config_;
    IApiClient& api_;
    OrderManager order_manager_;
    FairValueCalculator fv_calc_;
    ASParams as_params_;

    std::vector<MarketState> markets_;
    std::unordered_map<std::string, size_t> market_index_;

    bool balance_checked_ = false;
    std::atomic<bool> running_{false};
};

} // namespace mm
