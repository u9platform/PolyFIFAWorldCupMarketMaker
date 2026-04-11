#include "market_maker.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <ctime>

namespace mm {

static double yearsUntil(const std::string& date_str) {
    // Parse "YYYY-MM-DD" and compute fractional years from now.
    struct tm tm = {};
    if (date_str.size() >= 10) {
        tm.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(date_str.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(date_str.substr(8, 2));
    } else {
        // Default: 2026-07-20
        tm.tm_year = 126; tm.tm_mon = 6; tm.tm_mday = 20;
    }
    time_t expiry = mktime(&tm);
    time_t now = time(nullptr);
    double seconds = difftime(expiry, now);
    if (seconds < 0) seconds = 0;
    return seconds / (365.25 * 24 * 3600);
}

MarketMaker::MarketMaker(const Config& config, IApiClient& api)
    : config_(config)
    , api_(api)
    , order_manager_(api) {

    // Setup AS params from config
    as_params_.gamma = config.gamma;
    as_params_.min_spread = config.min_spread;
    as_params_.k = 5.0;  // will be updated from fill frequency
    as_params_.max_inventory = config.max_inventory;
    as_params_.time_to_expiry = yearsUntil(config.expiry_date);

    auto ids = config.allTokenIds();
    markets_.reserve(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        markets_.emplace_back(ids[i], config.vol_window_size);
        market_index_[ids[i]] = i;
    }
    spdlog::info("MarketMaker initialized: {} markets, gamma={}, min_spread={}, T-t={:.4f}y, max_inv={}",
                 markets_.size(), as_params_.gamma, as_params_.min_spread,
                 as_params_.time_to_expiry, as_params_.max_inventory);
}

void MarketMaker::start() {
    running_ = true;
    spdlog::info("MarketMaker starting for {} markets", markets_.size());

    while (running_) {
        try {
            // Update time to expiry each tick
            as_params_.time_to_expiry = yearsUntil(config_.expiry_date);
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

    for (auto& ms : markets_) {
        double mid = ms.last_reservation > 0 ? ms.last_reservation : 0;
        auto r = ms.reporter->generateReport(mid);
        spdlog::info("PnL [{}...]: realized={:.6f} unrealized={:.6f} total={:.6f} trades={} pos={:.0f}",
                     ms.token_id.substr(0, 10), r.realized_pnl, r.unrealized_pnl,
                     r.total_pnl, r.total_trades, ms.position.yesPosition());
    }
    spdlog::info("Portfolio exposure: {:.6f}", portfolioExposure());
}

void MarketMaker::tick() {
    if (!balance_checked_) {
        double balance = api_.getBalance();
        if (balance <= 0.0) {
            throw std::runtime_error("Insufficient balance: " + std::to_string(balance));
        }
        spdlog::info("Balance: {:.2f} USDC", balance);
        balance_checked_ = true;
    }

    // Check fills for all markets
    order_manager_.checkOrders([this](const std::string& order_id, Side side,
                                      double price, double qty) {
        for (auto& ms : markets_) {
            if (order_id == ms.active_bid_id) {
                ms.reporter->recordFill(Side::BUY, price, qty, nowMs());
                ms.active_bid_id.clear();
                spdlog::info("[{}...] BID FILLED {:.0f}@{:.4f} pos={:.0f}",
                             ms.token_id.substr(0, 10), qty, price, ms.position.yesPosition());
                return;
            }
            if (order_id == ms.active_ask_id) {
                ms.reporter->recordFill(Side::SELL, price, qty, nowMs());
                ms.active_ask_id.clear();
                spdlog::info("[{}...] ASK FILLED {:.0f}@{:.4f} pos={:.0f}",
                             ms.token_id.substr(0, 10), qty, price, ms.position.yesPosition());
                return;
            }
        }
    });

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

    double mid = ob.midPrice();

    // Update volatility tracker
    ms.vol_tracker.addPrice(mid);
    double sigma = ms.vol_tracker.sigma();

    // Fair value (currently = mid, ready for external data)
    double fair = fv_calc_.calculate(mid);

    // Inventory
    double inventory = ms.position.yesPosition();

    // AS calculation
    auto as_quote = QuoteEngine::calculateAS(fair, inventory, sigma, as_params_);
    if (!as_quote.has_value()) return;

    double new_r = as_quote->reservation_price;
    bool r_changed = QuoteEngine::shouldRequote(ms.last_reservation, new_r, config_.requote_threshold);
    bool missing_leg = (as_quote->bid_price > 0 && ms.active_bid_id.empty()) ||
                       (as_quote->ask_price > 0 && ms.active_ask_id.empty());
    bool need_requote = r_changed || missing_leg;

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

    ms.last_reservation = new_r;
    placeASQuote(ms, *as_quote);
}

void MarketMaker::placeASQuote(MarketState& ms, const ASQuote& quote) {
    std::string bid_id, ask_id;
    bool bid_placed = false, ask_placed = false;

    if (quote.bid_price > 0) {
        try {
            bid_id = order_manager_.placeOrder(ms.token_id, Side::BUY,
                                                quote.bid_price, config_.order_size);
            bid_placed = true;
        } catch (const ApiError& e) {
            spdlog::error("[{}...] Failed to place bid: {}", ms.token_id.substr(0, 10), e.what());
        }
    }

    if (quote.ask_price > 0) {
        try {
            ask_id = order_manager_.placeOrder(ms.token_id, Side::SELL,
                                                quote.ask_price, config_.order_size);
            ask_placed = true;
        } catch (const ApiError& e) {
            spdlog::error("[{}...] Failed to place ask: {}", ms.token_id.substr(0, 10), e.what());
            // Cancel bid if ask failed and we need both sides
            if (bid_placed) {
                try { order_manager_.cancelOrder(bid_id); } catch (...) {}
                bid_placed = false;
            }
        }
    }

    if (bid_placed) ms.active_bid_id = bid_id;
    if (ask_placed) ms.active_ask_id = ask_id;

    spdlog::info("[{}...] AS: r={:.4f} δ={:.4f} bid={:.4f} ask={:.4f} σ={:.4f} q={:.0f}",
                 ms.token_id.substr(0, 10),
                 quote.reservation_price, quote.optimal_spread,
                 quote.bid_price, quote.ask_price,
                 ms.vol_tracker.sigma(), ms.position.yesPosition());
}

const PositionTracker& MarketMaker::positionTracker() const {
    return markets_.at(0).position;
}

const PositionTracker& MarketMaker::positionTracker(const std::string& token_id) const {
    auto it = market_index_.find(token_id);
    return markets_.at(it->second).position;
}

double MarketMaker::lastMid() const {
    return markets_.empty() ? 0.0 : markets_[0].last_reservation;
}

double MarketMaker::lastMid(const std::string& token_id) const {
    auto it = market_index_.find(token_id);
    if (it == market_index_.end()) return 0.0;
    return markets_[it->second].last_reservation;
}

double MarketMaker::portfolioExposure() const {
    double total = 0;
    for (auto& ms : markets_) {
        total += ms.position.yesPosition() * ms.last_reservation;
    }
    return total;
}

} // namespace mm
