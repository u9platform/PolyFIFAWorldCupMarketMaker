#pragma once

#include "types.h"
#include "order_book.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace ix { class WebSocket; }

namespace mm {

struct BestQuote {
    double best_bid = 0;
    double best_ask = 0;
    int64_t timestamp_ms = 0;
};

// Callbacks include asset_id so caller knows which market updated.
using BookCallback = std::function<void(const std::string& asset_id, const OrderBook& book)>;
using PriceChangeCallback = std::function<void(const std::string& asset_id, const BestQuote& quote)>;
using TradeCallback = std::function<void(const std::string& asset_id, Side side, double price, double size)>;

class WsMarketFeed {
public:
    // Single token (backward compat)
    explicit WsMarketFeed(const std::string& token_id);
    // Multi-token: one WS connection, multiple subscriptions
    explicit WsMarketFeed(const std::vector<std::string>& token_ids);
    ~WsMarketFeed();

    void onBook(BookCallback cb) { book_cb_ = std::move(cb); }
    void onPriceChange(PriceChangeCallback cb) { price_cb_ = std::move(cb); }
    void onTrade(TradeCallback cb) { trade_cb_ = std::move(cb); }

    void start();
    void stop();

    // Get latest quote for a specific token (thread-safe).
    BestQuote latestQuote(const std::string& token_id) const;

    bool isConnected() const { return connected_; }

private:
    void run();
    void sendSubscribe();
    void processMessage(const std::string& msg);
    void processPriceChange(const nlohmann::json& data);

    std::vector<std::string> token_ids_;
    std::string ws_url_ = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
    ix::WebSocket* ws_ptr_ = nullptr;

    BookCallback book_cb_;
    PriceChangeCallback price_cb_;
    TradeCallback trade_cb_;

    std::thread ws_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    // Per-token latest quote, keyed by token_id
    mutable std::mutex quote_mutex_;
    std::unordered_map<std::string, BestQuote> latest_quotes_;

    // Fast lookup: is this token_id one we care about?
    std::unordered_set<std::string> token_set_;
};

} // namespace mm
