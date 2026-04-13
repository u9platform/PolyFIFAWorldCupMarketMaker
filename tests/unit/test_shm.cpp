#include "market_data_writer.h"
#include "market_data_reader.h"
#include <gtest/gtest.h>
#include <thread>
#include <sys/mman.h>

using namespace mm;

class ShmTest : public ::testing::Test {
protected:
    std::string shm_name = "/poly_md_test_" + std::to_string(getpid());

    void TearDown() override {
        shm_unlink(shm_name.c_str());
    }
};

TEST_F(ShmTest, WriterCreatesShm) {
    MarketDataWriter writer(shm_name, 16);
    EXPECT_EQ(writer.shm()->magic, SHM_MAGIC);
    EXPECT_EQ(writer.shm()->version, SHM_VERSION);
    EXPECT_EQ(writer.shm()->num_slots, 16u);
}

TEST_F(ShmTest, RegisterToken) {
    MarketDataWriter writer(shm_name);
    int idx = writer.registerToken("token_abc");
    EXPECT_EQ(idx, 0);
    int idx2 = writer.registerToken("token_def");
    EXPECT_EQ(idx2, 1);
    // Same token returns same index
    EXPECT_EQ(writer.registerToken("token_abc"), 0);
}

TEST_F(ShmTest, WriteAndRead) {
    MarketDataWriter writer(shm_name);
    writer.registerToken("token_123");
    writer.updateQuote("token_123", 0.021, 0.023, 1000);

    MarketDataReader reader(shm_name);
    MarketSlot slot;
    ASSERT_TRUE(reader.read("token_123", slot));
    EXPECT_DOUBLE_EQ(slot.best_bid, 0.021);
    EXPECT_DOUBLE_EQ(slot.best_ask, 0.023);
    EXPECT_DOUBLE_EQ(slot.mid, 0.022);
    EXPECT_EQ(slot.timestamp_ms, 1000);
}

TEST_F(ShmTest, MultipleTokens) {
    MarketDataWriter writer(shm_name);
    writer.registerToken("spain");
    writer.registerToken("japan");
    writer.updateQuote("spain", 0.154, 0.156, 2000);
    writer.updateQuote("japan", 0.021, 0.022, 2000);

    MarketDataReader reader(shm_name);
    MarketSlot s1, s2;
    ASSERT_TRUE(reader.read("spain", s1));
    ASSERT_TRUE(reader.read("japan", s2));
    EXPECT_DOUBLE_EQ(s1.best_bid, 0.154);
    EXPECT_DOUBLE_EQ(s2.best_bid, 0.021);
}

TEST_F(ShmTest, Heartbeat) {
    MarketDataWriter writer(shm_name);
    writer.heartbeat();

    MarketDataReader reader(shm_name);
    EXPECT_TRUE(reader.isAlive(5));
}

TEST_F(ShmTest, UnknownTokenReadFails) {
    MarketDataWriter writer(shm_name);
    writer.registerToken("known");

    MarketDataReader reader(shm_name);
    MarketSlot slot;
    EXPECT_FALSE(reader.read("unknown", slot));
}

TEST_F(ShmTest, ConcurrentWriteRead) {
    MarketDataWriter writer(shm_name);
    writer.registerToken("token_x");

    MarketDataReader reader(shm_name);

    // Writer thread: rapidly update quotes
    std::atomic<bool> done{false};
    std::thread writer_thread([&]() {
        for (int i = 0; i < 10000; i++) {
            double bid = 0.020 + (i % 10) * 0.001;
            double ask = bid + 0.001;
            writer.updateQuote("token_x", bid, ask, i);
        }
        done = true;
    });

    // Reader thread: read and verify consistency
    int reads = 0, consistent = 0;
    while (!done) {
        MarketSlot slot;
        if (reader.read("token_x", slot)) {
            reads++;
            // Check consistency: ask should be bid + 0.001
            if (std::abs(slot.best_ask - slot.best_bid - 0.001) < 1e-9) {
                consistent++;
            }
        }
    }

    writer_thread.join();

    EXPECT_GT(reads, 0);
    EXPECT_EQ(consistent, reads);  // All reads should be consistent
}
