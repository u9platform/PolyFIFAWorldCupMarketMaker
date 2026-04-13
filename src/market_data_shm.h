#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

namespace mm {

static constexpr int MAX_MARKETS = 64;
static constexpr uint32_t SHM_MAGIC = 0x504F4C59;  // "POLY"
static constexpr uint32_t SHM_VERSION = 1;

// One slot per token, cache-line aligned.
struct alignas(64) MarketSlot {
    std::atomic<uint64_t> sequence{0};  // seqlock: odd=writing, even=ready
    uint64_t token_hash = 0;           // hash of token_id string
    double best_bid = 0;
    double best_ask = 0;
    double mid = 0;
    int64_t timestamp_ms = 0;
    double last_trade_price = 0;
    int8_t last_trade_side = 0;        // 0=none, 1=BUY, 2=SELL
    double last_trade_size = 0;
    // Top-of-book depth
    double bid_prices[5] = {};
    double bid_sizes[5] = {};
    double ask_prices[5] = {};
    double ask_sizes[5] = {};
    char _pad[64];                     // pad to ~512 bytes
};

struct MarketDataShm {
    uint32_t magic = SHM_MAGIC;
    uint32_t version = SHM_VERSION;
    uint32_t num_slots = 0;
    int64_t heartbeat_ms = 0;          // updated by writer each second
    MarketSlot slots[MAX_MARKETS];
};

// Hash a token_id string to uint64 for fast slot lookup.
inline uint64_t hashToken(const std::string& token_id) {
    return std::hash<std::string>{}(token_id);
}

// Seqlock write helper.
inline void seqlockBeginWrite(MarketSlot& slot) {
    slot.sequence.store(slot.sequence.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
}

inline void seqlockEndWrite(MarketSlot& slot) {
    std::atomic_thread_fence(std::memory_order_release);
    slot.sequence.store(slot.sequence.load(std::memory_order_relaxed) + 1,
                        std::memory_order_release);
}

// Seqlock read: returns true if read was consistent.
inline bool seqlockTryRead(const MarketSlot& slot, MarketSlot& out) {
    uint64_t seq1 = slot.sequence.load(std::memory_order_acquire);
    if (seq1 & 1) return false;  // writer active

    // Force read ordering after sequence load
    std::atomic_thread_fence(std::memory_order_acquire);

    // Copy non-atomic fields
    out.token_hash = slot.token_hash;
    out.best_bid = slot.best_bid;
    out.best_ask = slot.best_ask;
    out.mid = slot.mid;
    out.timestamp_ms = slot.timestamp_ms;
    out.last_trade_price = slot.last_trade_price;
    out.last_trade_side = slot.last_trade_side;
    out.last_trade_size = slot.last_trade_size;
    std::memcpy(out.bid_prices, slot.bid_prices, sizeof(slot.bid_prices));
    std::memcpy(out.bid_sizes, slot.bid_sizes, sizeof(slot.bid_sizes));
    std::memcpy(out.ask_prices, slot.ask_prices, sizeof(slot.ask_prices));
    std::memcpy(out.ask_sizes, slot.ask_sizes, sizeof(slot.ask_sizes));

    // Force all reads to complete before checking sequence
    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t seq2 = slot.sequence.load(std::memory_order_relaxed);
    return seq1 == seq2;
}

// Read with spin-retry (bounded).
inline bool seqlockRead(const MarketSlot& slot, MarketSlot& out, int max_retries = 100) {
    for (int i = 0; i < max_retries; ++i) {
        if (seqlockTryRead(slot, out)) return true;
    }
    return false;
}

} // namespace mm
