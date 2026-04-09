#pragma once

#include "types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>

namespace mm {

class ApiError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Abstract interface for Polymarket CLOB API.
// Real implementation uses HTTP; tests use MockApiClient.
class IApiClient {
public:
    virtual ~IApiClient() = default;

    // Fetch order book for a given token.
    virtual nlohmann::json getOrderBook(const std::string& token_id) = 0;

    // Place a limit order. Returns order_id.
    virtual std::string placeOrder(const std::string& token_id, Side side,
                                   double price, double size) = 0;

    // Cancel an order by id.
    virtual void cancelOrder(const std::string& order_id) = 0;

    // Get status of an order.
    virtual OrderStatus getOrderStatus(const std::string& order_id) = 0;

    // Get filled quantity of an order.
    virtual double getFilledQty(const std::string& order_id) = 0;

    // Get available USDC balance.
    virtual double getBalance() = 0;
};

} // namespace mm
