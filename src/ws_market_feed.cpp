#include "ws_market_feed.h"
#include <ixwebsocket/IXWebSocket.h>
#include <spdlog/spdlog.h>

namespace mm {

WsMarketFeed::WsMarketFeed(const std::string& token_id)
    : token_id_(token_id) {}

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

BestQuote WsMarketFeed::latestQuote() const {
    std::lock_guard<std::mutex> lock(quote_mutex_);
    return latest_quote_;
}

void WsMarketFeed::sendSubscribe() {
    if (!ws_ptr_) return;
    nlohmann::json sub = {
        {"assets_ids", {token_id_}},
        {"type", "market"}
    };
    ws_ptr_->send(sub.dump());
    spdlog::info("WS subscribed to token {}", token_id_.substr(0, 20) + "...");
}

void WsMarketFeed::run() {
    ix::WebSocket ws;
    ws.setUrl(ws_url_);
    ws.setPingInterval(10);
    ws.setMinWaitBetweenReconnectionRetries(1000);   // 1s min between retries
    ws.setMaxWaitBetweenReconnectionRetries(5000);    // 5s max
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

        // Handle array of events
        std::vector<nlohmann::json> events;
        if (data.is_array()) {
            for (auto& e : data) events.push_back(e);
        } else {
            events.push_back(data);
        }

        for (auto& ev : events) {
            auto evt = ev.value("event_type", "");

            if (evt == "book") {
                // Full order book snapshot
                OrderBook ob;
                ob.parse(ev);
                if (ob.isValid()) {
                    {
                        std::lock_guard<std::mutex> lock(quote_mutex_);
                        latest_quote_.best_bid = ob.bestBid();
                        latest_quote_.best_ask = ob.bestAsk();
                        latest_quote_.timestamp_ms = std::stoll(ev.value("timestamp", "0"));
                    }
                    if (book_cb_) book_cb_(ob);
                }
            } else if (evt == "price_change") {
                processPriceChange(ev);
            } else if (evt == "last_trade_price") {
                if (trade_cb_) {
                    auto side_str = ev.value("side", "");
                    Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                    double price = std::stod(ev.value("price", "0"));
                    double size = std::stod(ev.value("size", "0"));
                    trade_cb_(side, price, size);
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("WS parse error: {}", e.what());
    }
}

void WsMarketFeed::processPriceChange(const nlohmann::json& data) {
    auto changes = data.value("price_changes", nlohmann::json::array());

    for (auto& c : changes) {
        auto asset_id = c.value("asset_id", "");
        if (asset_id != token_id_) continue;  // Only care about our token

        auto best_bid_str = c.value("best_bid", "");
        auto best_ask_str = c.value("best_ask", "");

        if (!best_bid_str.empty() && !best_ask_str.empty()) {
            BestQuote q;
            q.best_bid = std::stod(best_bid_str);
            q.best_ask = std::stod(best_ask_str);
            q.timestamp_ms = std::stoll(data.value("timestamp", "0"));

            {
                std::lock_guard<std::mutex> lock(quote_mutex_);
                latest_quote_ = q;
            }

            if (price_cb_) price_cb_(q);
        }
    }
}

} // namespace mm
