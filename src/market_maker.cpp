#include "market_maker.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace mm {

MarketMaker::MarketMaker(const Config& config, IApiClient& api)
    : config_(config)
    , api_(api)
    , order_manager_(api)
    , pnl_reporter_(position_tracker_) {}

void MarketMaker::start() {
    running_ = true;
    spdlog::info("MarketMaker starting for token {}", config_.market_token_id);

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
    if (!failed.empty()) {
        for (auto& id : failed) {
            spdlog::error("Failed to cancel order on shutdown: {}", id);
        }
    }

    auto report = pnl_reporter_.generateReport(last_mid_);
    spdlog::info("Final PnL Report: realized={:.6f} unrealized={:.6f} total={:.6f} trades={}",
                 report.realized_pnl, report.unrealized_pnl, report.total_pnl, report.total_trades);
}

void MarketMaker::tick() {
    // Step 0: Check balance on first tick
    if (last_mid_ == 0.0) {
        double balance = api_.getBalance();
        if (balance <= 0.0) {
            throw std::runtime_error("Insufficient balance: " + std::to_string(balance));
        }
        spdlog::info("Balance: {:.2f} USDC", balance);
    }

    // Step 1: Fetch order book
    auto ob_json = api_.getOrderBook(config_.market_token_id);
    OrderBook ob;
    ob.parse(ob_json);

    // Step 2: Check for fills (always, every tick)
    bool had_fills = false;
    order_manager_.checkOrders([&](const std::string& id, Side side, double price, double qty) {
        pnl_reporter_.recordFill(side, price, qty, nowMs());
        had_fills = true;

        if (side == Side::BUY && id == active_bid_id_) {
            active_bid_id_.clear();
        } else if (side == Side::SELL && id == active_ask_id_) {
            active_ask_id_.clear();
        }
    });

    // Step 3: Handle invalid order book
    if (!ob.isValid()) {
        spdlog::warn("Invalid order book, canceling existing orders");
        if (!active_bid_id_.empty() || !active_ask_id_.empty()) {
            order_manager_.cancelAll();
            active_bid_id_.clear();
            active_ask_id_.clear();
        }
        return;
    }

    // Step 4: Check if requote needed
    double new_mid = ob.midPrice();
    bool mid_changed = QuoteEngine::shouldRequote(last_mid_, new_mid, config_.requote_threshold);
    bool missing_leg = active_bid_id_.empty() || active_ask_id_.empty();
    bool need_requote = mid_changed || had_fills || missing_leg;

    if (!need_requote) {
        return;
    }

    // Step 5: Cancel existing orders
    if (!active_bid_id_.empty()) {
        try {
            order_manager_.cancelOrder(active_bid_id_);
        } catch (...) {}
        active_bid_id_.clear();
    }
    if (!active_ask_id_.empty()) {
        try {
            order_manager_.cancelOrder(active_ask_id_);
        } catch (...) {}
        active_ask_id_.clear();
    }

    // Step 6: Calculate new quotes and place
    last_mid_ = new_mid;
    auto quote = QuoteEngine::calculateQuotes(new_mid, config_.spread);
    if (!quote.has_value()) {
        spdlog::warn("Cannot calculate quotes for mid={}", new_mid);
        return;
    }

    placeBothSides(*quote);
}

void MarketMaker::placeBothSides(const Quote& quote) {
    std::string bid_id, ask_id;

    try {
        bid_id = order_manager_.placeOrder(
            config_.market_token_id, Side::BUY, quote.bid_price, config_.order_size);
    } catch (const ApiError& e) {
        spdlog::error("Failed to place bid: {}", e.what());
        return;
    }

    try {
        ask_id = order_manager_.placeOrder(
            config_.market_token_id, Side::SELL, quote.ask_price, config_.order_size);
    } catch (const ApiError& e) {
        spdlog::error("Failed to place ask: {}, canceling bid {}", e.what(), bid_id);
        try {
            order_manager_.cancelOrder(bid_id);
        } catch (...) {
            spdlog::error("Failed to cancel orphaned bid {}", bid_id);
        }
        return;
    }

    active_bid_id_ = bid_id;
    active_ask_id_ = ask_id;
    spdlog::info("Quotes placed: bid={:.4f} ask={:.4f} mid={:.4f}",
                 quote.bid_price, quote.ask_price, last_mid_);
}

} // namespace mm
