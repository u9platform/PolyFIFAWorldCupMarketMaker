#pragma once

#include "types.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace mm {

struct PriceLevel {
    double price;
    double size;
};

class OrderBook {
public:
    // Parse order book from CLOB API JSON response.
    void parse(const nlohmann::json& json);

    bool isValid() const;

    double bestBid() const;
    double bestAsk() const;
    double bestBidSize() const;
    double bestAskSize() const;

    double midPrice() const;
    double microPrice() const;

    const std::vector<PriceLevel>& bids() const { return bids_; }
    const std::vector<PriceLevel>& asks() const { return asks_; }

private:
    std::vector<PriceLevel> bids_;  // sorted descending by price
    std::vector<PriceLevel> asks_;  // sorted ascending by price
};

} // namespace mm
