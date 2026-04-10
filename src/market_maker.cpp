#include "market_maker.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace mm {

MarketMaker::MarketMaker(const Config& config, IApiClient& api)
    : config_(config)
    , api_(api)
    , order_manager_(api) {

    auto ids = config.allTokenIds();
    markets_.reserve(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        markets_.emplace_back(ids[i]);
        market_index_[ids[i]] = i;
    }
    spdlog::info("MarketMaker initialized with {} markets", markets_.size());
}

void MarketMaker::start() {
    running_ = true;
    spdlog::info("MarketMaker starting for {} markets", markets_.size());

    while (running_) {
        try {
            tick();
        } catch (const std::exception& e) {
            spdlog::error("Tick error: {}", e.what());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
    }
}

void MarketMaker::stop() {
    running_ = false;
    spdlog::info("Stopping MarketMaker, canceling all orders...");
    auto failed = order_manager_.cancelAll();
    for (auto& id : failed) {
        spdlog::error("Failed to cancel order on shutdown: {}", id);
    }

    // Print per-market PnL
    for (auto& ms : markets_) {
        double mid = ms.last_mid > 0 ? ms.last_mid : 0;
        auto r = ms.reporter->generateReport(mid);
        spdlog::info("PnL [{}...]: realized={:.6f} unrealized={:.6f} total={:.6f} trades={}",
                     ms.token_id.substr(0, 10), r.realized_pnl, r.unrealized_pnl,
                     r.total_pnl, r.total_trades);
    }
    spdlog::info("Portfolio exposure: {:.6f}", portfolioExposure());
}

void MarketMaker::tick() {
    // Check balance once
    if (!balance_checked_) {
        double balance = api_.getBalance();
        if (balance <= 0.0) {
            throw std::runtime_error("Insufficient balance: " + std::to_string(balance));
        }
        spdlog::info("Balance: {:.2f} USDC", balance);
        balance_checked_ = true;
    }

    // Check fills for ALL markets first (one call checks all active orders)
    order_manager_.checkOrders([this](const std::string& order_id, Side side,
                                      double price, double qty) {
        // Find which market this order belongs to
        for (auto& ms : markets_) {
            if (order_id == ms.active_bid_id) {
                ms.reporter->recordFill(Side::BUY, price, qty, nowMs());
                ms.active_bid_id.clear();
                spdlog::info("[{}...] BID FILLED {:.0f}@{:.4f}", ms.token_id.substr(0, 10), qty, price);
                return;
            }
            if (order_id == ms.active_ask_id) {
                ms.reporter->recordFill(Side::SELL, price, qty, nowMs());
                ms.active_ask_id.clear();
                spdlog::info("[{}...] ASK FILLED {:.0f}@{:.4f}", ms.token_id.substr(0, 10), qty, price);
                return;
            }
        }
    });

    // Tick each market
    for (auto& ms : markets_) {
        tickSingleMarket(ms);
    }
}

void MarketMaker::tickMarket(const std::string& token_id) {
    auto it = market_index_.find(token_id);
    if (it == market_index_.end()) return;
    tickSingleMarket(markets_[it->second]);
}

void MarketMaker::tickSingleMarket(MarketState& ms) {
    // Fetch order book
    nlohmann::json ob_json;
    try {
        ob_json = api_.getOrderBook(ms.token_id);
    } catch (const std::exception& e) {
        spdlog::warn("[{}...] Failed to fetch book: {}", ms.token_id.substr(0, 10), e.what());
        return;
    }

    OrderBook ob;
    ob.parse(ob_json);

    if (!ob.isValid()) {
        if (!ms.active_bid_id.empty() || !ms.active_ask_id.empty()) {
            if (!ms.active_bid_id.empty()) {
                try { order_manager_.cancelOrder(ms.active_bid_id); } catch (...) {}
                ms.active_bid_id.clear();
            }
            if (!ms.active_ask_id.empty()) {
                try { order_manager_.cancelOrder(ms.active_ask_id); } catch (...) {}
                ms.active_ask_id.clear();
            }
        }
        return;
    }

    double new_mid = ob.midPrice();
    bool mid_changed = QuoteEngine::shouldRequote(ms.last_mid, new_mid, config_.requote_threshold);
    bool missing_leg = ms.active_bid_id.empty() || ms.active_ask_id.empty();
    bool need_requote = mid_changed || missing_leg;

    if (!need_requote) return;

    // Cancel existing
    if (!ms.active_bid_id.empty()) {
        try { order_manager_.cancelOrder(ms.active_bid_id); } catch (...) {}
        ms.active_bid_id.clear();
    }
    if (!ms.active_ask_id.empty()) {
        try { order_manager_.cancelOrder(ms.active_ask_id); } catch (...) {}
        ms.active_ask_id.clear();
    }

    ms.last_mid = new_mid;
    auto quote = QuoteEngine::calculateQuotes(new_mid, config_.spread);
    if (!quote.has_value()) return;

    placeBothSides(ms, *quote);
}

void MarketMaker::placeBothSides(MarketState& ms, const Quote& quote) {
    std::string bid_id, ask_id;

    try {
        bid_id = order_manager_.placeOrder(ms.token_id, Side::BUY, quote.bid_price, config_.order_size);
    } catch (const ApiError& e) {
        spdlog::error("[{}...] Failed to place bid: {}", ms.token_id.substr(0, 10), e.what());
        return;
    }

    try {
        ask_id = order_manager_.placeOrder(ms.token_id, Side::SELL, quote.ask_price, config_.order_size);
    } catch (const ApiError& e) {
        spdlog::error("[{}...] Failed to place ask, canceling bid", ms.token_id.substr(0, 10));
        try { order_manager_.cancelOrder(bid_id); } catch (...) {}
        return;
    }

    ms.active_bid_id = bid_id;
    ms.active_ask_id = ask_id;
    spdlog::info("[{}...] Quotes: bid={:.4f} ask={:.4f} mid={:.4f}",
                 ms.token_id.substr(0, 10), quote.bid_price, quote.ask_price, ms.last_mid);
}

// Backward compat accessors (first market)
const PositionTracker& MarketMaker::positionTracker() const {
    return markets_.at(0).position;
}

const PositionTracker& MarketMaker::positionTracker(const std::string& token_id) const {
    auto it = market_index_.find(token_id);
    return markets_.at(it->second).position;
}

double MarketMaker::lastMid() const {
    return markets_.empty() ? 0.0 : markets_[0].last_mid;
}

double MarketMaker::lastMid(const std::string& token_id) const {
    auto it = market_index_.find(token_id);
    if (it == market_index_.end()) return 0.0;
    return markets_[it->second].last_mid;
}

double MarketMaker::portfolioExposure() const {
    double total = 0;
    for (auto& ms : markets_) {
        total += ms.position.yesPosition() * ms.last_mid;
    }
    return total;
}

} // namespace mm
