#pragma once

#include "api_client.h"
#include <string>

namespace mm {

class RealApiClient : public IApiClient {
public:
    explicit RealApiClient(const std::string& api_key = "",
                           const std::string& api_secret = "",
                           const std::string& private_key = "");
    ~RealApiClient() override;

    // Public endpoints (no auth required)
    nlohmann::json getOrderBook(const std::string& token_id) override;
    double getBalance() override;

    // Authenticated endpoints (not yet implemented)
    std::string placeOrder(const std::string& token_id, Side side,
                           double price, double size) override;
    void cancelOrder(const std::string& order_id) override;
    OrderStatus getOrderStatus(const std::string& order_id) override;
    double getFilledQty(const std::string& order_id) override;

private:
    std::string httpGet(const std::string& url);

    std::string base_url_ = "https://clob.polymarket.com";
    std::string api_key_;
    std::string api_secret_;
    std::string private_key_;
};

} // namespace mm
