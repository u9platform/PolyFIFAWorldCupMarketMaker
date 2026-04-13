#include "market_data_writer.h"
#include "types.h"
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

namespace mm {

MarketDataWriter::MarketDataWriter(const std::string& shm_name, int num_slots)
    : shm_name_(shm_name) {

    shm_size_ = sizeof(MarketDataShm);

    // Create shared memory
    fd_ = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) {
        throw std::runtime_error("shm_open failed: " + std::string(strerror(errno)));
    }

    if (ftruncate(fd_, shm_size_) < 0) {
        close(fd_);
        throw std::runtime_error("ftruncate failed: " + std::string(strerror(errno)));
    }

    void* ptr = mmap(nullptr, shm_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr == MAP_FAILED) {
        close(fd_);
        throw std::runtime_error("mmap failed: " + std::string(strerror(errno)));
    }

    shm_ = new (ptr) MarketDataShm();
    shm_->magic = SHM_MAGIC;
    shm_->version = SHM_VERSION;
    shm_->num_slots = std::min(num_slots, MAX_MARKETS);
    shm_->heartbeat_ms = nowMs();

    spdlog::info("SHM writer created: {} ({} slots, {} bytes)",
                 shm_name, shm_->num_slots, shm_size_);
}

MarketDataWriter::~MarketDataWriter() {
    if (shm_) munmap(shm_, shm_size_);
    if (fd_ >= 0) close(fd_);
    shm_unlink(shm_name_.c_str());
}

int MarketDataWriter::registerToken(const std::string& token_id) {
    auto it = token_to_slot_.find(token_id);
    if (it != token_to_slot_.end()) return it->second;

    int idx = token_to_slot_.size();
    if (idx >= static_cast<int>(shm_->num_slots)) {
        throw std::runtime_error("Too many tokens, max=" + std::to_string(shm_->num_slots));
    }

    token_to_slot_[token_id] = idx;
    shm_->slots[idx].token_hash = hashToken(token_id);
    spdlog::info("SHM registered token slot[{}]: {}...", idx, token_id.substr(0, 20));
    return idx;
}

MarketSlot* MarketDataWriter::getSlot(const std::string& token_id) {
    auto it = token_to_slot_.find(token_id);
    if (it == token_to_slot_.end()) return nullptr;
    return &shm_->slots[it->second];
}

void MarketDataWriter::updateQuote(const std::string& token_id,
                                    double best_bid, double best_ask,
                                    int64_t timestamp_ms) {
    auto* slot = getSlot(token_id);
    if (!slot) return;

    seqlockBeginWrite(*slot);
    slot->best_bid = best_bid;
    slot->best_ask = best_ask;
    slot->mid = (best_bid + best_ask) / 2.0;
    slot->timestamp_ms = timestamp_ms;
    seqlockEndWrite(*slot);
}

void MarketDataWriter::updateTrade(const std::string& token_id,
                                    double price, int8_t side, double size,
                                    int64_t timestamp_ms) {
    auto* slot = getSlot(token_id);
    if (!slot) return;

    seqlockBeginWrite(*slot);
    slot->last_trade_price = price;
    slot->last_trade_side = side;
    slot->last_trade_size = size;
    slot->timestamp_ms = timestamp_ms;
    seqlockEndWrite(*slot);
}

void MarketDataWriter::updateDepth(const std::string& token_id,
                                    const double* bp, const double* bs,
                                    const double* ap, const double* as,
                                    int levels) {
    auto* slot = getSlot(token_id);
    if (!slot) return;

    int n = std::min(levels, 5);
    seqlockBeginWrite(*slot);
    std::memcpy(slot->bid_prices, bp, n * sizeof(double));
    std::memcpy(slot->bid_sizes, bs, n * sizeof(double));
    std::memcpy(slot->ask_prices, ap, n * sizeof(double));
    std::memcpy(slot->ask_sizes, as, n * sizeof(double));
    seqlockEndWrite(*slot);
}

void MarketDataWriter::heartbeat() {
    if (shm_) shm_->heartbeat_ms = nowMs();
}

} // namespace mm
