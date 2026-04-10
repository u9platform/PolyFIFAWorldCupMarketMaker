#pragma once

#include "real_api_client.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <atomic>

namespace mm {

// Wraps RealApiClient for market data, simulates order placement.
// Orders are tracked locally and "filled" based on market price.
class DryRunApiClient : public IApiClient {
public:
    DryRunApiClient() = default;

    // Real market data
    nlohmann::json getOrderBook(const std::string& token_id) override {
        return real_.getOrderBook(token_id);
    }

    double getBalance() override { return balance_; }

    // Simulated order placement
    std::string placeOrder(const std::string& token_id, Side side,
                           double price, double size) override {
        std::string id = "DRY-" + std::to_string(next_id_++);
        orders_[id] = SimOrder{side, price, size, 0.0, OrderStatus::LIVE};
        spdlog::info("[DRY] Placed {} {} {:.0f}@{:.4f} id={}",
                     side == Side::BUY ? "BUY" : "SELL", token_id.substr(0, 10) + "...",
                     size, price, id);
        return id;
    }

    void cancelOrder(const std::string& order_id) override {
        auto it = orders_.find(order_id);
        if (it != orders_.end()) {
            it->second.status = OrderStatus::CANCELED;
            orders_.erase(it);
            spdlog::info("[DRY] Canceled {}", order_id);
        }
    }

    OrderStatus getOrderStatus(const std::string& order_id) override {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return OrderStatus::UNKNOWN;
        return it->second.status;
    }

    double getFilledQty(const std::string& order_id) override {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return 0.0;
        return it->second.filled_qty;
    }

    // Call this with current market prices to simulate fills.
    // If a BUY order's price >= best_ask, it gets filled.
    // If a SELL order's price <= best_bid, it gets filled.
    void simulateFills(double best_bid, double best_ask) {
        for (auto& [id, order] : orders_) {
            if (order.status != OrderStatus::LIVE) continue;

            bool should_fill = false;
            if (order.side == Side::BUY && order.price >= best_ask) {
                should_fill = true;
            } else if (order.side == Side::SELL && order.price <= best_bid) {
                should_fill = true;
            }

            if (should_fill) {
                order.filled_qty = order.size;
                order.status = OrderStatus::FILLED;
                spdlog::info("[DRY] FILLED {} {:.0f}@{:.4f} id={}",
                             order.side == Side::BUY ? "BUY" : "SELL",
                             order.size, order.price, id);
            }
        }
    }

private:
    struct SimOrder {
        Side side;
        double price;
        double size;
        double filled_qty;
        OrderStatus status;
    };

    RealApiClient real_;
    std::unordered_map<std::string, SimOrder> orders_;
    std::atomic<int> next_id_{1};
    double balance_ = 1000.0;
};

} // namespace mm
