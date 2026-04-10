#pragma once

#include "api_client.h"
#include <string>
#include <mutex>
#include <curl/curl.h>

namespace mm {

class RealApiClient : public IApiClient {
public:
    explicit RealApiClient(const std::string& api_key = "",
                           const std::string& api_secret = "",
                           const std::string& private_key = "");
    ~RealApiClient() override;

    // Not copyable (owns curl handle)
    RealApiClient(const RealApiClient&) = delete;
    RealApiClient& operator=(const RealApiClient&) = delete;

    nlohmann::json getOrderBook(const std::string& token_id) override;
    double getBalance() override;

    // Authenticated (not yet implemented)
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

    CURL* curl_ = nullptr;          // Persistent handle — connection reuse
    std::mutex curl_mutex_;          // Serialize access from multiple threads
    struct curl_slist* headers_ = nullptr;
};

} // namespace mm
