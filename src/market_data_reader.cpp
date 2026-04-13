#include "market_data_reader.h"
#include "types.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>

namespace mm {

MarketDataReader::MarketDataReader(const std::string& shm_name)
    : shm_name_(shm_name) {

    shm_size_ = sizeof(MarketDataShm);

    fd_ = shm_open(shm_name.c_str(), O_RDONLY, 0);
    if (fd_ < 0) {
        throw std::runtime_error("shm_open (reader) failed: " + std::string(strerror(errno)));
    }

    void* ptr = mmap(nullptr, shm_size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (ptr == MAP_FAILED) {
        close(fd_);
        throw std::runtime_error("mmap (reader) failed: " + std::string(strerror(errno)));
    }

    shm_ = static_cast<const MarketDataShm*>(ptr);

    if (shm_->magic != SHM_MAGIC) {
        munmap(const_cast<void*>(static_cast<const void*>(shm_)), shm_size_);
        close(fd_);
        throw std::runtime_error("Invalid SHM magic number");
    }
}

MarketDataReader::~MarketDataReader() {
    if (shm_) munmap(const_cast<void*>(static_cast<const void*>(shm_)), shm_size_);
    if (fd_ >= 0) close(fd_);
}

void MarketDataReader::buildIndex() const {
    hash_to_slot_.clear();
    for (uint32_t i = 0; i < shm_->num_slots; ++i) {
        uint64_t h = shm_->slots[i].token_hash;
        if (h != 0) {
            hash_to_slot_[h] = i;
        }
    }
    index_built_ = true;
}

bool MarketDataReader::read(const std::string& token_id, MarketSlot& out) const {
    if (!index_built_) buildIndex();

    uint64_t h = hashToken(token_id);
    auto it = hash_to_slot_.find(h);
    if (it == hash_to_slot_.end()) {
        // Rebuild index in case new tokens registered
        buildIndex();
        it = hash_to_slot_.find(h);
        if (it == hash_to_slot_.end()) return false;
    }

    return seqlockRead(shm_->slots[it->second], out);
}

bool MarketDataReader::isAlive(int timeout_seconds) const {
    if (!shm_) return false;
    int64_t now = nowMs();
    int64_t age = now - shm_->heartbeat_ms;
    return age < static_cast<int64_t>(timeout_seconds) * 1000;
}

uint32_t MarketDataReader::numSlots() const {
    return shm_ ? shm_->num_slots : 0;
}

} // namespace mm
