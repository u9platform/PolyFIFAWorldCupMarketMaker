#pragma once

#include "market_data_shm.h"
#include <string>
#include <unordered_map>

namespace mm {

class MarketDataReader {
public:
    // Opens existing shared memory (read-only).
    explicit MarketDataReader(const std::string& shm_name);
    ~MarketDataReader();

    MarketDataReader(const MarketDataReader&) = delete;
    MarketDataReader& operator=(const MarketDataReader&) = delete;

    // Read latest data for a token. Returns true if successful.
    bool read(const std::string& token_id, MarketSlot& out) const;

    // Check if writer is alive (heartbeat within last N seconds).
    bool isAlive(int timeout_seconds = 5) const;

    // Number of registered slots.
    uint32_t numSlots() const;

private:
    std::string shm_name_;
    int fd_ = -1;
    const MarketDataShm* shm_ = nullptr;
    size_t shm_size_ = 0;

    // Cache: token_id -> slot index (built on first read)
    mutable std::unordered_map<uint64_t, int> hash_to_slot_;
    mutable bool index_built_ = false;
    void buildIndex() const;
};

} // namespace mm
