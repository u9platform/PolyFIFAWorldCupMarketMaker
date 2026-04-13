#pragma once

#include "market_data_shm.h"
#include <string>
#include <unordered_map>

namespace mm {

class MarketDataWriter {
public:
    // Creates or opens shared memory segment.
    explicit MarketDataWriter(const std::string& shm_name, int num_slots = MAX_MARKETS);
    ~MarketDataWriter();

    MarketDataWriter(const MarketDataWriter&) = delete;
    MarketDataWriter& operator=(const MarketDataWriter&) = delete;

    // Register a token and assign it a slot index.
    int registerToken(const std::string& token_id);

    // Write market data for a token. Thread-safe via seqlock.
    void updateQuote(const std::string& token_id,
                     double best_bid, double best_ask,
                     int64_t timestamp_ms);

    void updateTrade(const std::string& token_id,
                     double price, int8_t side, double size,
                     int64_t timestamp_ms);

    void updateDepth(const std::string& token_id,
                     const double* bid_prices, const double* bid_sizes,
                     const double* ask_prices, const double* ask_sizes,
                     int levels);

    // Update heartbeat (call every second).
    void heartbeat();

    MarketDataShm* shm() { return shm_; }

private:
    MarketSlot* getSlot(const std::string& token_id);

    std::string shm_name_;
    int fd_ = -1;
    MarketDataShm* shm_ = nullptr;
    size_t shm_size_ = 0;
    std::unordered_map<std::string, int> token_to_slot_;
};

} // namespace mm
