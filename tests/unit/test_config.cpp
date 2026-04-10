#include "config.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

using namespace mm;

class ConfigTest : public ::testing::Test {
protected:
    std::string tmp_path;

    void SetUp() override {
        tmp_path = "/tmp/mm_test_config_" + std::to_string(getpid()) + ".json";
    }

    void TearDown() override {
        std::remove(tmp_path.c_str());
    }

    void writeConfig(const std::string& content) {
        std::ofstream f(tmp_path);
        f << content;
    }
};

TEST_F(ConfigTest, LoadDefaults) {
    writeConfig(R"({
        "api_key": "test_key",
        "api_secret": "test_secret",
        "private_key": "test_pk",
        "market_token_id": "12345"
    })");
    auto cfg = Config::load(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.spread, 0.002);
    EXPECT_DOUBLE_EQ(cfg.order_size, 100.0);
    EXPECT_EQ(cfg.poll_interval_ms, 10000);
}

TEST_F(ConfigTest, LoadCustomValues) {
    writeConfig(R"({
        "api_key": "test_key",
        "api_secret": "test_secret",
        "private_key": "test_pk",
        "market_token_id": "12345",
        "spread": 0.004,
        "order_size": 200
    })");
    auto cfg = Config::load(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.spread, 0.004);
    EXPECT_DOUBLE_EQ(cfg.order_size, 200.0);
    EXPECT_EQ(cfg.poll_interval_ms, 10000);  // default
}

TEST_F(ConfigTest, InvalidSpread_Negative) {
    writeConfig(R"({
        "api_key": "k", "api_secret": "s", "private_key": "p",
        "market_token_id": "t",
        "spread": -0.001
    })");
    EXPECT_THROW(Config::load(tmp_path), ConfigError);
}

TEST_F(ConfigTest, InvalidSpread_Zero) {
    writeConfig(R"({
        "api_key": "k", "api_secret": "s", "private_key": "p",
        "market_token_id": "t",
        "spread": 0
    })");
    EXPECT_THROW(Config::load(tmp_path), ConfigError);
}

TEST_F(ConfigTest, InvalidSpread_TooLarge) {
    writeConfig(R"({
        "api_key": "k", "api_secret": "s", "private_key": "p",
        "market_token_id": "t",
        "spread": 0.5
    })");
    EXPECT_THROW(Config::load(tmp_path), ConfigError);
}

TEST_F(ConfigTest, MissingRequiredField) {
    writeConfig(R"({
        "api_secret": "s", "private_key": "p", "market_token_id": "t"
    })");
    EXPECT_THROW(Config::load(tmp_path), ConfigError);
}

TEST_F(ConfigTest, FileNotFound) {
    EXPECT_THROW(Config::load("/tmp/nonexistent_config_12345.json"), ConfigError);
}

TEST_F(ConfigTest, MultiMarketTokenIds) {
    writeConfig(R"({
        "api_key": "k", "api_secret": "s", "private_key": "p",
        "market_token_ids": ["token1", "token2", "token3"]
    })");
    auto cfg = Config::load(tmp_path);
    auto ids = cfg.allTokenIds();
    EXPECT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], "token1");
    EXPECT_EQ(ids[2], "token3");
}

TEST_F(ConfigTest, SingleMarketBackwardCompat) {
    writeConfig(R"({
        "api_key": "k", "api_secret": "s", "private_key": "p",
        "market_token_id": "single_token"
    })");
    auto cfg = Config::load(tmp_path);
    auto ids = cfg.allTokenIds();
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], "single_token");
}

TEST_F(ConfigTest, NoMarketTokenAtAll) {
    writeConfig(R"({
        "api_key": "k", "api_secret": "s", "private_key": "p"
    })");
    EXPECT_THROW(Config::load(tmp_path), ConfigError);
}
