#include "ws_market_feed.h"
#include <ixwebsocket/IXWebSocket.h>
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace mm {

WsMarketFeed::WsMarketFeed(const std::string& token_id)
    : token_ids_({token_id}), token_set_({token_id}) {}

WsMarketFeed::WsMarketFeed(const std::vector<std::string>& token_ids)
    : token_ids_(token_ids), token_set_(token_ids.begin(), token_ids.end()) {}

WsMarketFeed::~WsMarketFeed() {
    stop();
}

void WsMarketFeed::start() {
    if (running_) return;
    running_ = true;
    ws_thread_ = std::thread(&WsMarketFeed::run, this);
}

void WsMarketFeed::stop() {
    running_ = false;
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

BestQuote WsMarketFeed::latestQuote(const std::string& token_id) const {
    std::lock_guard<std::mutex> lock(quote_mutex_);
    auto it = latest_quotes_.find(token_id);
    if (it != latest_quotes_.end()) return it->second;
    return {};
}

void WsMarketFeed::sendSubscribe() {
    if (!ws_ptr_) return;
    nlohmann::json sub = {
        {"assets_ids", token_ids_},
        {"type", "market"}
    };
    ws_ptr_->send(sub.dump());
    spdlog::info("WS subscribed to {} tokens", token_ids_.size());
}

void WsMarketFeed::run() {
    ix::WebSocket ws;
    ws.setUrl(ws_url_);
    ws.setPingInterval(10);
    ws.setMinWaitBetweenReconnectionRetries(1000);
    ws.setMaxWaitBetweenReconnectionRetries(5000);
    ws_ptr_ = &ws;

    ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
        case ix::WebSocketMessageType::Open:
            connected_ = true;
            spdlog::info("WS connected to {}", ws_url_);
            sendSubscribe();
            break;
        case ix::WebSocketMessageType::Message:
            processMessage(msg->str);
            break;
        case ix::WebSocketMessageType::Close:
            connected_ = false;
            spdlog::warn("WS disconnected: {} {}", msg->closeInfo.code, msg->closeInfo.reason);
            break;
        case ix::WebSocketMessageType::Error:
            spdlog::error("WS error: {}", msg->errorInfo.reason);
            break;
        default:
            break;
        }
    });

    ws.start();

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ws.stop();
    ws_ptr_ = nullptr;
    connected_ = false;
    spdlog::info("WS feed stopped");
}

void WsMarketFeed::processMessage(const std::string& msg) {
    if (msg == "PONG") return;

    try {
        auto data = nlohmann::json::parse(msg);

        // Handle both single object and array
        if (data.is_array()) {
            for (auto& ev : data) {
                if (!ev.is_object()) continue;
                auto evt = ev.value("event_type", "");
                if (evt == "book") {
                    auto asset_id = ev.value("asset_id", "");
                    if (token_set_.count(asset_id) == 0) continue;
                    OrderBook ob;
                    ob.parse(ev);
                    if (ob.isValid()) {
                        BestQuote q{ob.bestBid(), ob.bestAsk(),
                                    std::stoll(ev.value("timestamp", "0"))};
                        {
                            std::lock_guard<std::mutex> lock(quote_mutex_);
                            latest_quotes_[asset_id] = q;
                        }
                        if (book_cb_) book_cb_(asset_id, ob);
                    }
                } else if (evt == "price_change") {
                    processPriceChange(ev);
                } else if (evt == "last_trade_price") {
                    auto asset_id = ev.value("asset_id", "");
                    if (token_set_.count(asset_id) == 0) continue;
                    if (trade_cb_) {
                        Side side = ev.value("side", "") == "BUY" ? Side::BUY : Side::SELL;
                        trade_cb_(asset_id, side,
                                  std::stod(ev.value("price", "0")),
                                  std::stod(ev.value("size", "0")));
                    }
                }
            }
        } else if (data.is_object()) {
            auto evt = data.value("event_type", "");
            if (evt == "book") {
                auto asset_id = data.value("asset_id", "");
                if (token_set_.count(asset_id) == 0) return;
                OrderBook ob;
                ob.parse(data);
                if (ob.isValid()) {
                    BestQuote q{ob.bestBid(), ob.bestAsk(),
                                std::stoll(data.value("timestamp", "0"))};
                    {
                        std::lock_guard<std::mutex> lock(quote_mutex_);
                        latest_quotes_[asset_id] = q;
                    }
                    if (book_cb_) book_cb_(asset_id, ob);
                }
            } else if (evt == "price_change") {
                processPriceChange(data);
            } else if (evt == "last_trade_price") {
                auto asset_id = data.value("asset_id", "");
                if (token_set_.count(asset_id) == 0) return;
                if (trade_cb_) {
                    Side side = data.value("side", "") == "BUY" ? Side::BUY : Side::SELL;
                    trade_cb_(asset_id, side,
                              std::stod(data.value("price", "0")),
                              std::stod(data.value("size", "0")));
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("WS parse error: {}", e.what());
    }
}

void WsMarketFeed::processPriceChange(const nlohmann::json& data) {
    auto ts = std::stoll(data.value("timestamp", "0"));
    auto& changes = data["price_changes"];

    for (auto& c : changes) {
        auto asset_id = c.value("asset_id", "");
        if (token_set_.count(asset_id) == 0) continue;

        auto best_bid_str = c.value("best_bid", "");
        auto best_ask_str = c.value("best_ask", "");
        if (best_bid_str.empty() || best_ask_str.empty()) continue;

        BestQuote q{std::stod(best_bid_str), std::stod(best_ask_str), ts};
        {
            std::lock_guard<std::mutex> lock(quote_mutex_);
            latest_quotes_[asset_id] = q;
        }
        if (price_cb_) price_cb_(asset_id, q);
    }
}

} // namespace mm
