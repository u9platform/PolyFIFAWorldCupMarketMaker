#pragma once

#include "types.h"
#include "order_book.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

namespace mm {

struct BestQuote {
    double best_bid = 0;
    double best_ask = 0;
    int64_t timestamp_ms = 0;
};

// Callback types for WebSocket events
using BookCallback = std::function<void(const OrderBook& book)>;
using PriceChangeCallback = std::function<void(const BestQuote& quote)>;
using TradeCallback = std::function<void(Side side, double price, double size)>;

class WsMarketFeed {
public:
    explicit WsMarketFeed(const std::string& token_id);
    ~WsMarketFeed();

    void onBook(BookCallback cb) { book_cb_ = std::move(cb); }
    void onPriceChange(PriceChangeCallback cb) { price_cb_ = std::move(cb); }
    void onTrade(TradeCallback cb) { trade_cb_ = std::move(cb); }

    // Start WebSocket connection in background thread.
    void start();
    void stop();

    // Get latest best bid/ask (thread-safe).
    BestQuote latestQuote() const;

    bool isConnected() const { return connected_; }

private:
    void run();
    void processMessage(const std::string& msg);
    void processPriceChange(const nlohmann::json& data);

    std::string token_id_;
    std::string ws_url_ = "wss://ws-subscriptions-clob.polymarket.com/ws/market";

    BookCallback book_cb_;
    PriceChangeCallback price_cb_;
    TradeCallback trade_cb_;

    std::thread ws_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    mutable std::mutex quote_mutex_;
    BestQuote latest_quote_;
};

} // namespace mm
