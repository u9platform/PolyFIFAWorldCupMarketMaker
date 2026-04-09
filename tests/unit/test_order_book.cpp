#include "order_book.h"
#include <gtest/gtest.h>

using namespace mm;
using json = nlohmann::json;

static json makeBook(json bids, json asks) {
    return json{{"bids", bids}, {"asks", asks}};
}

TEST(OrderBook, ParseNormalData) {
    auto j = makeBook(
        {{{"price", "0.021"}, {"size", "500"}}},
        {{{"price", "0.023"}, {"size", "300"}}}
    );
    OrderBook ob;
    ob.parse(j);
    EXPECT_TRUE(ob.isValid());
    EXPECT_DOUBLE_EQ(ob.bestBid(), 0.021);
    EXPECT_DOUBLE_EQ(ob.bestBidSize(), 500.0);
    EXPECT_DOUBLE_EQ(ob.bestAsk(), 0.023);
    EXPECT_DOUBLE_EQ(ob.bestAskSize(), 300.0);
}

TEST(OrderBook, CalculateMidPrice) {
    auto j = makeBook(
        {{{"price", "0.021"}, {"size", "500"}}},
        {{{"price", "0.023"}, {"size", "300"}}}
    );
    OrderBook ob;
    ob.parse(j);
    EXPECT_DOUBLE_EQ(ob.midPrice(), 0.022);
}

TEST(OrderBook, CalculateMicroPrice) {
    auto j = makeBook(
        {{{"price", "0.021"}, {"size", "500"}}},
        {{{"price", "0.023"}, {"size", "300"}}}
    );
    OrderBook ob;
    ob.parse(j);
    // micro = (0.021 * 300 + 0.023 * 500) / 800 = 17.8 / 800 = 0.02225
    EXPECT_NEAR(ob.microPrice(), 0.02225, 1e-9);
}

TEST(OrderBook, EmptyOrderBook) {
    auto j = makeBook(json::array(), json::array());
    OrderBook ob;
    ob.parse(j);
    EXPECT_FALSE(ob.isValid());
    EXPECT_DOUBLE_EQ(ob.midPrice(), 0.0);
}

TEST(OrderBook, SingleSideMissing) {
    auto j = makeBook(
        {{{"price", "0.021"}, {"size", "500"}}},
        json::array()
    );
    OrderBook ob;
    ob.parse(j);
    EXPECT_FALSE(ob.isValid());
}

TEST(OrderBook, MultipleDepthLevels) {
    auto j = makeBook(
        {{{"price", "0.021"}, {"size", "500"}},
         {{"price", "0.020"}, {"size", "300"}},
         {{"price", "0.019"}, {"size", "200"}}},
        {{{"price", "0.023"}, {"size", "400"}},
         {{"price", "0.024"}, {"size", "200"}},
         {{"price", "0.025"}, {"size", "100"}}}
    );
    OrderBook ob;
    ob.parse(j);
    EXPECT_TRUE(ob.isValid());
    EXPECT_DOUBLE_EQ(ob.bestBid(), 0.021);
    EXPECT_DOUBLE_EQ(ob.bestAsk(), 0.023);
    EXPECT_EQ(ob.bids().size(), 3u);
    EXPECT_EQ(ob.asks().size(), 3u);
}

TEST(OrderBook, CrossedMarket) {
    auto j = makeBook(
        {{{"price", "0.023"}, {"size", "500"}}},
        {{{"price", "0.021"}, {"size", "300"}}}
    );
    OrderBook ob;
    ob.parse(j);
    EXPECT_FALSE(ob.isValid());
}

TEST(OrderBook, LockedMarket) {
    auto j = makeBook(
        {{{"price", "0.022"}, {"size", "500"}}},
        {{{"price", "0.022"}, {"size", "300"}}}
    );
    OrderBook ob;
    ob.parse(j);
    EXPECT_FALSE(ob.isValid());
}
