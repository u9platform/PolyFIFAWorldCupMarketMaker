#pragma once

#include "real_api_client.h"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <atomic>
#include <mutex>

namespace mm {

class DryRunApiClient : public IApiClient {
public:
    DryRunApiClient() = default;

    nlohmann::json getOrderBook(const std::string& token_id) override {
        return real_.getOrderBook(token_id);
    }

    double getBalance() override { return balance_; }

    std::string placeOrder(const std::string& token_id, Side side,
                           double price, double size) override {
        std::string id = "DRY-" + std::to_string(next_id_++);
        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_[id] = SimOrder{token_id, side, price, size, 0.0, OrderStatus::LIVE};
        spdlog::info("[DRY] Placed {} {} {:.0f}@{:.4f} id={}",
                     side == Side::BUY ? "BUY" : "SELL",
                     token_id.substr(0, 10) + "...", size, price, id);
        return id;
    }

    void cancelOrder(const std::string& order_id) override {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        orders_.erase(order_id);
        spdlog::info("[DRY] Canceled {}", order_id);
    }

    OrderStatus getOrderStatus(const std::string& order_id) override {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return OrderStatus::UNKNOWN;
        return it->second.status;
    }

    double getFilledQty(const std::string& order_id) override {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return 0.0;
        return it->second.filled_qty;
    }

    // Simulate fills for a specific token based on market prices.
    void simulateFills(const std::string& token_id, double best_bid, double best_ask) {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        for (auto& [id, order] : orders_) {
            if (order.status != OrderStatus::LIVE) continue;
            if (order.token_id != token_id) continue;

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
        std::string token_id;
        Side side;
        double price;
        double size;
        double filled_qty;
        OrderStatus status;
    };

    RealApiClient real_;
    std::mutex orders_mutex_;
    std::unordered_map<std::string, SimOrder> orders_;
    std::atomic<int> next_id_{1};
    double balance_ = 1000.0;
};

} // namespace mm
