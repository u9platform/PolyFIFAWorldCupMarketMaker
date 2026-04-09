#pragma once

#include "api_client.h"
#include <gmock/gmock.h>

namespace mm {

class MockApiClient : public IApiClient {
public:
    MOCK_METHOD(nlohmann::json, getOrderBook, (const std::string& token_id), (override));
    MOCK_METHOD(std::string, placeOrder,
                (const std::string& token_id, Side side, double price, double size), (override));
    MOCK_METHOD(void, cancelOrder, (const std::string& order_id), (override));
    MOCK_METHOD(OrderStatus, getOrderStatus, (const std::string& order_id), (override));
    MOCK_METHOD(double, getFilledQty, (const std::string& order_id), (override));
    MOCK_METHOD(double, getBalance, (), (override));
};

} // namespace mm
