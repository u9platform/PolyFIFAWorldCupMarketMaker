#include "order_manager.h"
#include <spdlog/spdlog.h>
#include <thread>

namespace mm {

OrderManager::OrderManager(IApiClient& api, int max_retries)
    : api_(api), max_retries_(max_retries) {}

std::string OrderManager::placeOrder(const std::string& token_id, Side side,
                                      double price, double size) {
    ApiError last_error("unknown");
    for (int attempt = 0; attempt < max_retries_; ++attempt) {
        try {
            auto order_id = api_.placeOrder(token_id, side, price, size);
            active_orders_[order_id] = OrderInfo{order_id, side, price, size, 0.0};
            spdlog::info("Placed order {} {} {}@{}", order_id,
                         side == Side::BUY ? "BUY" : "SELL", size, price);
            return order_id;
        } catch (const ApiError& e) {
            last_error = e;
            spdlog::warn("placeOrder attempt {}/{} failed: {}", attempt + 1, max_retries_, e.what());
        }
    }
    throw last_error;
}

void OrderManager::cancelOrder(const std::string& order_id) {
    try {
        api_.cancelOrder(order_id);
        active_orders_.erase(order_id);
        spdlog::info("Canceled order {}", order_id);
    } catch (const ApiError& e) {
        spdlog::error("Failed to cancel order {}: {}", order_id, e.what());
        throw;
    }
}

std::vector<std::string> OrderManager::cancelAll() {
    std::vector<std::string> failed;
    // Copy keys first since we modify the map
    std::vector<std::string> ids;
    ids.reserve(active_orders_.size());
    for (auto& [id, _] : active_orders_) {
        ids.push_back(id);
    }

    for (auto& id : ids) {
        try {
            api_.cancelOrder(id);
            active_orders_.erase(id);
            spdlog::info("Canceled order {}", id);
        } catch (const ApiError& e) {
            spdlog::error("Failed to cancel order {}: {}", id, e.what());
            failed.push_back(id);
        }
    }
    return failed;
}

void OrderManager::checkOrders(FillCallback on_fill) {
    // Copy keys since callbacks might trigger new orders
    std::vector<std::string> ids;
    for (auto& [id, _] : active_orders_) {
        ids.push_back(id);
    }

    for (auto& id : ids) {
        if (active_orders_.find(id) == active_orders_.end()) continue;

        auto status = api_.getOrderStatus(id);
        auto& info = active_orders_[id];

        if (status == OrderStatus::FILLED) {
            double total_filled = api_.getFilledQty(id);
            double new_fill = total_filled - info.filled_qty;
            if (new_fill <= 0) new_fill = info.qty - info.filled_qty;  // fallback
            spdlog::info("Order {} FILLED, qty={}", id, new_fill);
            on_fill(id, info.side, info.price, new_fill);
            active_orders_.erase(id);
        } else if (status == OrderStatus::PARTIALLY_FILLED) {
            double total_filled = api_.getFilledQty(id);
            double new_fill = total_filled - info.filled_qty;
            if (new_fill > 0) {
                spdlog::info("Order {} PARTIAL FILL, new_qty={}", id, new_fill);
                on_fill(id, info.side, info.price, new_fill);
                info.filled_qty = total_filled;
            }
        }
    }
}

} // namespace mm
