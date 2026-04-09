#pragma once

#include "api_client.h"
#include "types.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

class OrderManager {
public:
    explicit OrderManager(IApiClient& api, int max_retries = 3);

    // Place order with retry. Throws ApiError after max retries.
    std::string placeOrder(const std::string& token_id, Side side,
                           double price, double size);

    // Cancel a single order. Throws ApiError on failure.
    void cancelOrder(const std::string& order_id);

    // Cancel all active orders. Returns list of order_ids that failed to cancel.
    std::vector<std::string> cancelAll();

    // Check all active orders for fills. Calls on_fill for each detected fill.
    using FillCallback = std::function<void(const std::string& order_id, Side side,
                                            double price, double qty)>;
    void checkOrders(FillCallback on_fill);

    const std::unordered_map<std::string, OrderInfo>& activeOrders() const {
        return active_orders_;
    }

private:
    IApiClient& api_;
    int max_retries_;
    std::unordered_map<std::string, OrderInfo> active_orders_;
};

} // namespace mm
