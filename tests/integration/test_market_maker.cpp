#include "market_maker.h"
#include "mock_api_client.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace mm;
using ::testing::_;
using ::testing::Return;
using ::testing::Throw;
using ::testing::AtLeast;
using json = nlohmann::json;

static json validBook(double bid, double ask) {
    return json{
        {"bids", {{{"price", std::to_string(bid)}, {"size", "1000"}}}},
        {"asks", {{{"price", std::to_string(ask)}, {"size", "1000"}}}}
    };
}

class MarketMakerTest : public ::testing::Test {
protected:
    MockApiClient mock_api;
    Config config;

    void SetUp() override {
        config.market_token_id = "test_token";
        config.spread = 0.002;
        config.order_size = 100;
        config.requote_threshold = 0.001;
        config.api_key = "key";
        config.api_secret = "secret";
        config.private_key = "pk";
    }
};

TEST_F(MarketMakerTest, StartPlacesBidAndAsk) {
    EXPECT_CALL(mock_api, getBalance()).WillOnce(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_)).WillOnce(Return(validBook(0.021, 0.023)));
    EXPECT_CALL(mock_api, placeOrder(_, Side::BUY, _, _)).WillOnce(Return("bid1"));
    EXPECT_CALL(mock_api, placeOrder(_, Side::SELL, _, _)).WillOnce(Return("ask1"));
    // checkOrders will see LIVE orders
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));

    MarketMaker mm(config, mock_api);
    mm.tick();

    EXPECT_EQ(mm.orderManager().activeOrders().size(), 2u);
}

TEST_F(MarketMakerTest, StartInsufficientBalance) {
    EXPECT_CALL(mock_api, getBalance()).WillOnce(Return(0.0));

    MarketMaker mm(config, mock_api);
    EXPECT_THROW(mm.tick(), std::runtime_error);
}

TEST_F(MarketMakerTest, MidChangeTriggersRequote) {
    // First tick: place orders at mid=0.022
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillOnce(Return(validBook(0.021, 0.023)))   // tick 1: mid=0.022
        .WillOnce(Return(validBook(0.023, 0.025)));   // tick 2: mid=0.024
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))   // tick 1 bid
        .WillOnce(Return("ask1"))   // tick 1 ask
        .WillOnce(Return("bid2"))   // tick 2 bid (requote)
        .WillOnce(Return("ask2"));  // tick 2 ask (requote)
    EXPECT_CALL(mock_api, cancelOrder(_)).Times(2);  // cancel old bid+ask
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));

    MarketMaker mm(config, mock_api);
    mm.tick();  // places initial orders
    mm.tick();  // mid changed -> requote

    EXPECT_NEAR(mm.lastMid(), 0.024, 1e-9);
}

TEST_F(MarketMakerTest, MidNoChange_NoRequote) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillOnce(Return(validBook(0.021, 0.023)))
        .WillOnce(Return(validBook(0.0215, 0.0235)));  // mid=0.0225, delta < threshold
    // Only 2 placeOrder calls total (initial placement only)
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("ask1"));
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));

    MarketMaker mm(config, mock_api);
    mm.tick();
    mm.tick();  // no requote

    EXPECT_EQ(mm.orderManager().activeOrders().size(), 2u);
}

TEST_F(MarketMakerTest, BidFilledTriggersRequote) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillRepeatedly(Return(validBook(0.021, 0.023)));

    // tick 1: bid1 + ask1. tick 2: bid1 filled → cancel ask1 → place bid2 + ask2
    EXPECT_CALL(mock_api, placeOrder(_, Side::BUY, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("bid2"));
    EXPECT_CALL(mock_api, placeOrder(_, Side::SELL, _, _))
        .WillOnce(Return("ask1"))
        .WillOnce(Return("ask2"));
    EXPECT_CALL(mock_api, cancelOrder("ask1")).Times(1);

    EXPECT_CALL(mock_api, getOrderStatus("bid1")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("bid1")).WillOnce(Return(100.0));
    EXPECT_CALL(mock_api, getOrderStatus("ask1")).WillOnce(Return(OrderStatus::LIVE));

    MarketMaker mm(config, mock_api);
    mm.tick();
    mm.tick();

    EXPECT_DOUBLE_EQ(mm.positionTracker().yesPosition(), 100.0);
}

TEST_F(MarketMakerTest, AskFilledTriggersRequote) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillRepeatedly(Return(validBook(0.021, 0.023)));

    // tick 1: bid1 + ask1. tick 2: ask1 filled → cancel bid1 → place bid2 + ask2
    EXPECT_CALL(mock_api, placeOrder(_, Side::BUY, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("bid2"));
    EXPECT_CALL(mock_api, placeOrder(_, Side::SELL, _, _))
        .WillOnce(Return("ask1"))
        .WillOnce(Return("ask2"));
    EXPECT_CALL(mock_api, cancelOrder("bid1")).Times(1);

    EXPECT_CALL(mock_api, getOrderStatus("bid1")).WillOnce(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("ask1")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("ask1")).WillOnce(Return(100.0));

    MarketMaker mm(config, mock_api);
    mm.tick();
    mm.tick();

    EXPECT_DOUBLE_EQ(mm.positionTracker().yesPosition(), -100.0);
}

TEST_F(MarketMakerTest, BothSidesFilled) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillRepeatedly(Return(validBook(0.021, 0.023)));

    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("ask1"))
        .WillOnce(Return("bid2"))
        .WillOnce(Return("ask2"));

    EXPECT_CALL(mock_api, getOrderStatus("bid1")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("bid1")).WillOnce(Return(100.0));
    EXPECT_CALL(mock_api, getOrderStatus("ask1")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("ask1")).WillOnce(Return(100.0));

    MarketMaker mm(config, mock_api);
    mm.tick();
    mm.tick();

    EXPECT_DOUBLE_EQ(mm.positionTracker().yesPosition(), 0.0);
    EXPECT_GT(mm.positionTracker().realizedPnl(), 0.0);
}

TEST_F(MarketMakerTest, MidChangeAndFillSimultaneous) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillOnce(Return(validBook(0.021, 0.023)))   // tick 1
        .WillOnce(Return(validBook(0.023, 0.025)));   // tick 2: mid changed

    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("ask1"))
        .WillOnce(Return("bid2"))   // new bid at new mid
        .WillOnce(Return("ask2"));  // new ask at new mid

    // tick 2: bid1 filled, ask1 still live
    EXPECT_CALL(mock_api, getOrderStatus("bid1")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("bid1")).WillOnce(Return(100.0));
    EXPECT_CALL(mock_api, getOrderStatus("ask1")).WillOnce(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder("ask1")).Times(1);

    MarketMaker mm(config, mock_api);
    mm.tick();  // initial placement (checkOrders empty map)
    mm.tick();  // fill detected AND mid changed -> both handled

    EXPECT_DOUBLE_EQ(mm.positionTracker().yesPosition(), 100.0);
    EXPECT_NEAR(mm.lastMid(), 0.024, 1e-9);
}

TEST_F(MarketMakerTest, InvalidOrderBook_NoQuote) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillOnce(Return(json{{"bids", json::array()}, {"asks", json::array()}}));
    // No placeOrder calls expected
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _)).Times(0);

    MarketMaker mm(config, mock_api);
    mm.tick();  // should not place orders
}

TEST_F(MarketMakerTest, CrossedOrderBook_CancelAndNoQuote) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    // First tick: valid book
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillOnce(Return(validBook(0.021, 0.023)))
        .WillOnce(Return(validBook(0.025, 0.021)));  // crossed!

    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("ask1"));
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder(_)).Times(2);  // cancel existing orders

    MarketMaker mm(config, mock_api);
    mm.tick();  // place orders
    mm.tick();  // crossed book -> cancel all, don't place new
}

TEST_F(MarketMakerTest, SingleLegPlacementFailure) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillOnce(Return(validBook(0.021, 0.023)));

    EXPECT_CALL(mock_api, placeOrder(_, Side::BUY, _, _))
        .WillOnce(Return("bid1"));
    EXPECT_CALL(mock_api, placeOrder(_, Side::SELL, _, _))
        .WillRepeatedly(Throw(ApiError("failed")));
    // Should cancel the successful bid
    EXPECT_CALL(mock_api, cancelOrder("bid1")).Times(1);

    MarketMaker mm(config, mock_api);
    mm.tick();

    EXPECT_TRUE(mm.orderManager().activeOrders().empty());
}

TEST_F(MarketMakerTest, GracefulShutdown) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillRepeatedly(Return(validBook(0.021, 0.023)));
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("ask1"));
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder(_)).Times(2);

    MarketMaker mm(config, mock_api);
    mm.tick();

    mm.stop();  // triggers cancelAll
    EXPECT_TRUE(mm.orderManager().activeOrders().empty());
}

TEST_F(MarketMakerTest, GracefulShutdown_CancelPartialFailure) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_))
        .WillRepeatedly(Return(validBook(0.021, 0.023)));
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("bid1"))
        .WillOnce(Return("ask1"));
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder("bid1")).Times(1);
    EXPECT_CALL(mock_api, cancelOrder("ask1")).WillOnce(Throw(ApiError("fail")));

    MarketMaker mm(config, mock_api);
    mm.tick();

    // stop should not throw, but log the failure
    EXPECT_NO_THROW(mm.stop());
}

// === Multi-market tests ===

class MultiMarketTest : public ::testing::Test {
protected:
    MockApiClient mock_api;
    Config config;

    void SetUp() override {
        config.market_token_ids = {"tokenA", "tokenB", "tokenC"};
        config.spread = 0.002;
        config.order_size = 100;
        config.requote_threshold = 0.001;
        config.api_key = "key";
        config.api_secret = "secret";
        config.private_key = "pk";
    }
};

TEST_F(MultiMarketTest, InitializesAllMarkets) {
    MarketMaker mm(config, mock_api);
    EXPECT_EQ(mm.marketCount(), 3u);
}

TEST_F(MultiMarketTest, EachMarketGetsIndependentQuotes) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook("tokenA")).WillOnce(Return(validBook(0.021, 0.023)));
    EXPECT_CALL(mock_api, getOrderBook("tokenB")).WillOnce(Return(validBook(0.150, 0.160)));
    EXPECT_CALL(mock_api, getOrderBook("tokenC")).WillOnce(Return(validBook(0.030, 0.040)));
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("a_bid")).WillOnce(Return("a_ask"))
        .WillOnce(Return("b_bid")).WillOnce(Return("b_ask"))
        .WillOnce(Return("c_bid")).WillOnce(Return("c_ask"));
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));

    MarketMaker mm(config, mock_api);
    mm.tick();

    EXPECT_EQ(mm.orderManager().activeOrders().size(), 6u);
    EXPECT_NEAR(mm.lastMid("tokenA"), 0.022, 1e-9);
    EXPECT_NEAR(mm.lastMid("tokenB"), 0.155, 1e-9);
    EXPECT_NEAR(mm.lastMid("tokenC"), 0.035, 1e-9);
}

TEST_F(MultiMarketTest, FillInOneMarketDoesNotAffectOthers) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook("tokenA")).WillRepeatedly(Return(validBook(0.021, 0.023)));
    EXPECT_CALL(mock_api, getOrderBook("tokenB")).WillRepeatedly(Return(validBook(0.150, 0.160)));
    EXPECT_CALL(mock_api, getOrderBook("tokenC")).WillRepeatedly(Return(validBook(0.030, 0.040)));

    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("a_bid")).WillOnce(Return("a_ask"))
        .WillOnce(Return("b_bid")).WillOnce(Return("b_ask"))
        .WillOnce(Return("c_bid")).WillOnce(Return("c_ask"))
        // After fill in A: re-place A's bid+ask
        .WillOnce(Return("a_bid2")).WillOnce(Return("a_ask2"));

    // Tick 1: all LIVE
    // Tick 2: a_bid filled, rest LIVE
    EXPECT_CALL(mock_api, getOrderStatus("a_bid")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("a_bid")).WillOnce(Return(100.0));
    EXPECT_CALL(mock_api, getOrderStatus("a_ask"))
        .WillOnce(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder("a_ask")).Times(1);
    EXPECT_CALL(mock_api, getOrderStatus("b_bid")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("b_ask")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("c_bid")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("c_ask")).WillRepeatedly(Return(OrderStatus::LIVE));

    MarketMaker mm(config, mock_api);
    mm.tick();  // initial placement
    mm.tick();  // a_bid fills

    EXPECT_DOUBLE_EQ(mm.positionTracker("tokenA").yesPosition(), 100.0);
    EXPECT_DOUBLE_EQ(mm.positionTracker("tokenB").yesPosition(), 0.0);
    EXPECT_DOUBLE_EQ(mm.positionTracker("tokenC").yesPosition(), 0.0);
}

TEST_F(MultiMarketTest, PortfolioExposure) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook("tokenA")).WillRepeatedly(Return(validBook(0.019, 0.021)));
    EXPECT_CALL(mock_api, getOrderBook("tokenB")).WillRepeatedly(Return(validBook(0.140, 0.160)));
    EXPECT_CALL(mock_api, getOrderBook("tokenC")).WillRepeatedly(Return(validBook(0.030, 0.040)));

    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("a_bid")).WillOnce(Return("a_ask"))
        .WillOnce(Return("b_bid")).WillOnce(Return("b_ask"))
        .WillOnce(Return("c_bid")).WillOnce(Return("c_ask"))
        .WillRepeatedly(Return("x"));  // for re-placements

    // a_bid fills (buy 100 @ tokenA), b_ask fills (sell 50 @ tokenB)
    EXPECT_CALL(mock_api, getOrderStatus("a_bid")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("a_bid")).WillOnce(Return(100.0));
    EXPECT_CALL(mock_api, getOrderStatus("b_ask")).WillOnce(Return(OrderStatus::FILLED));
    EXPECT_CALL(mock_api, getFilledQty("b_ask")).WillOnce(Return(50.0));
    EXPECT_CALL(mock_api, getOrderStatus("a_ask")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("b_bid")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("c_bid")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, getOrderStatus("c_ask")).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder(_)).WillRepeatedly(Return());

    MarketMaker mm(config, mock_api);
    mm.tick();
    mm.tick();

    // tokenA: +100, mid=0.020. tokenB: -50, mid=0.150
    // exposure = 100*0.020 + (-50)*0.150 = 2.0 - 7.5 = -5.5
    EXPECT_NEAR(mm.portfolioExposure(), 100 * 0.020 + (-50) * 0.150, 0.01);
}

TEST_F(MultiMarketTest, GracefulShutdownAllMarkets) {
    EXPECT_CALL(mock_api, getBalance()).WillRepeatedly(Return(1000.0));
    EXPECT_CALL(mock_api, getOrderBook(_)).WillRepeatedly(Return(validBook(0.021, 0.023)));
    EXPECT_CALL(mock_api, placeOrder(_, _, _, _))
        .WillOnce(Return("a_bid")).WillOnce(Return("a_ask"))
        .WillOnce(Return("b_bid")).WillOnce(Return("b_ask"))
        .WillOnce(Return("c_bid")).WillOnce(Return("c_ask"));
    EXPECT_CALL(mock_api, getOrderStatus(_)).WillRepeatedly(Return(OrderStatus::LIVE));
    EXPECT_CALL(mock_api, cancelOrder(_)).Times(6);

    MarketMaker mm(config, mock_api);
    mm.tick();

    mm.stop();
    EXPECT_TRUE(mm.orderManager().activeOrders().empty());
}
