#include "order_manager.h"
#include "mock_api_client.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace mm;
using ::testing::_;
using ::testing::Return;
using ::testing::Throw;
using ::testing::InSequence;

class OrderManagerTest : public ::testing::Test {
protected:
    MockApiClient mock_api;
    OrderManager mgr{mock_api, 3};
};

TEST_F(OrderManagerTest, PlaceOrderSuccess) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("abc123"));

    auto id = mgr.placeOrder("token1", Side::BUY, 0.021, 100);
    EXPECT_EQ(id, "abc123");
    EXPECT_EQ(mgr.activeOrders().count("abc123"), 1u);
}

TEST_F(OrderManagerTest, PlaceOrderRetry_EventualSuccess) {
    InSequence seq;
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Throw(ApiError("500")))
        .WillOnce(Throw(ApiError("500")))
        .WillOnce(Return("abc123"));

    auto id = mgr.placeOrder("token1", Side::BUY, 0.021, 100);
    EXPECT_EQ(id, "abc123");
}

TEST_F(OrderManagerTest, PlaceOrderRetry_AllFail) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillRepeatedly(Throw(ApiError("500")));

    EXPECT_THROW(mgr.placeOrder("token1", Side::BUY, 0.021, 100), ApiError);
}

TEST_F(OrderManagerTest, CancelOrderSuccess) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _)).WillOnce(Return("abc123"));
    EXPECT_CALL(mock_api, cancelOrder("abc123")).Times(1);

    mgr.placeOrder("token1", Side::BUY, 0.021, 100);
    mgr.cancelOrder("abc123");
    EXPECT_EQ(mgr.activeOrders().count("abc123"), 0u);
}

TEST_F(OrderManagerTest, CancelOrderFailure) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _)).WillOnce(Return("abc123"));
    EXPECT_CALL(mock_api, cancelOrder("abc123")).WillOnce(Throw(ApiError("500")));

    mgr.placeOrder("token1", Side::BUY, 0.021, 100);
    EXPECT_THROW(mgr.cancelOrder("abc123"), ApiError);
    // Order should still be in active list
    EXPECT_EQ(mgr.activeOrders().count("abc123"), 1u);
}

TEST_F(OrderManagerTest, CancelAll) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("o1"))
        .WillOnce(Return("o2"))
        .WillOnce(Return("o3"));
    EXPECT_CALL(mock_api, cancelOrder(_)).Times(3);

    mgr.placeOrder("t", Side::BUY, 0.021, 100);
    mgr.placeOrder("t", Side::SELL, 0.023, 100);
    mgr.placeOrder("t", Side::BUY, 0.020, 100);

    auto failed = mgr.cancelAll();
    EXPECT_TRUE(failed.empty());
    EXPECT_TRUE(mgr.activeOrders().empty());
}

TEST_F(OrderManagerTest, CancelAll_PartialFailure) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("o1"))
        .WillOnce(Return("o2"))
        .WillOnce(Return("o3"));

    // o2 cancel fails, others succeed
    EXPECT_CALL(mock_api, cancelOrder("o1")).Times(1);
    EXPECT_CALL(mock_api, cancelOrder("o2")).WillOnce(Throw(ApiError("500")));
    EXPECT_CALL(mock_api, cancelOrder("o3")).Times(1);

    mgr.placeOrder("t", Side::BUY, 0.021, 100);
    mgr.placeOrder("t", Side::SELL, 0.023, 100);
    mgr.placeOrder("t", Side::BUY, 0.020, 100);

    auto failed = mgr.cancelAll();
    EXPECT_EQ(failed.size(), 1u);
    EXPECT_EQ(failed[0], "o2");
}

TEST_F(OrderManagerTest, CheckOrders_FullFill) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _)).WillOnce(Return("abc123"));
    EXPECT_CALL(mock_api, getOrderStatus("abc123")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("abc123")).WillOnce(Return(100.0));

    mgr.placeOrder("t", Side::BUY, 0.021, 100);

    bool called = false;
    mgr.checkOrders([&](const std::string& id, Side side, double price, double qty) {
        EXPECT_EQ(id, "abc123");
        EXPECT_EQ(side, Side::BUY);
        EXPECT_DOUBLE_EQ(price, 0.021);
        EXPECT_DOUBLE_EQ(qty, 100.0);
        called = true;
    });

    EXPECT_TRUE(called);
    EXPECT_EQ(mgr.activeOrders().count("abc123"), 0u);
}

TEST_F(OrderManagerTest, CheckOrders_PartialFill) {
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _)).WillOnce(Return("abc123"));
    EXPECT_CALL(mock_api, getOrderStatus("abc123")).WillOnce(Return(OrderStatus::PARTIALLY_FILLED));
    EXPECT_CALL(mock_api, getFilledQty("abc123")).WillOnce(Return(50.0));

    mgr.placeOrder("t", Side::BUY, 0.021, 100);

    double filled = 0;
    mgr.checkOrders([&](const std::string&, Side, double, double qty) {
        filled = qty;
    });

    EXPECT_DOUBLE_EQ(filled, 50.0);
    // Order remains active
    EXPECT_EQ(mgr.activeOrders().count("abc123"), 1u);
}
