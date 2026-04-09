#include "order_book.h"
#include <algorithm>

namespace mm {

void OrderBook::parse(const nlohmann::json& json) {
    bids_.clear();
    asks_.clear();

    if (json.contains("bids")) {
        for (auto& b : json["bids"]) {
            bids_.push_back({
                std::stod(b["price"].get<std::string>()),
                std::stod(b["size"].get<std::string>())
            });
        }
    }

    if (json.contains("asks")) {
        for (auto& a : json["asks"]) {
            asks_.push_back({
                std::stod(a["price"].get<std::string>()),
                std::stod(a["size"].get<std::string>())
            });
        }
    }

    // Sort bids descending, asks ascending
    std::sort(bids_.begin(), bids_.end(),
              [](const PriceLevel& a, const PriceLevel& b) { return a.price > b.price; });
    std::sort(asks_.begin(), asks_.end(),
              [](const PriceLevel& a, const PriceLevel& b) { return a.price < b.price; });
}

bool OrderBook::isValid() const {
    if (bids_.empty() || asks_.empty()) return false;
    // Crossed or locked market is invalid
    if (bids_[0].price >= asks_[0].price) return false;
    return true;
}

double OrderBook::bestBid() const {
    return bids_.empty() ? 0.0 : bids_[0].price;
}

double OrderBook::bestAsk() const {
    return asks_.empty() ? 0.0 : asks_[0].price;
}

double OrderBook::bestBidSize() const {
    return bids_.empty() ? 0.0 : bids_[0].size;
}

double OrderBook::bestAskSize() const {
    return asks_.empty() ? 0.0 : asks_[0].size;
}

double OrderBook::midPrice() const {
    if (!isValid()) return 0.0;
    return (bestBid() + bestAsk()) / 2.0;
}

double OrderBook::microPrice() const {
    if (!isValid()) return 0.0;
    double bid = bestBid();
    double ask = bestAsk();
    double bid_sz = bestBidSize();
    double ask_sz = bestAskSize();
    double total = bid_sz + ask_sz;
    if (total <= 0.0) return midPrice();
    return (bid * ask_sz + ask * bid_sz) / total;
}

} // namespace mm
