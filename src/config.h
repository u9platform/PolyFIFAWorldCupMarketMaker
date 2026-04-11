#pragma once

#include <string>
#include <vector>
#include <stdexcept>

namespace mm {

class ConfigError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Config {
    // Single market (backward compat) or multi-market
    std::string market_token_id;              // single market mode
    std::vector<std::string> market_token_ids; // multi-market mode

    double spread = 0.002;       // used only in legacy fixed-spread mode
    double order_size = 100.0;

    // AS model params
    double gamma = 0.1;
    double min_spread = 0.001;
    int vol_window_size = 100;
    double max_inventory = 1000;
    std::string expiry_date = "2026-07-20";
    int poll_interval_ms = 10000;
    double requote_threshold = 0.001;
    std::string api_key;
    std::string api_secret;
    std::string private_key;
    std::string log_file = "mm.log";
    int pnl_report_interval_s = 60;

    // Returns all token IDs (merges single + multi).
    std::vector<std::string> allTokenIds() const {
        if (!market_token_ids.empty()) return market_token_ids;
        if (!market_token_id.empty()) return {market_token_id};
        return {};
    }

    static Config load(const std::string& filepath);
    void validate() const;
};

} // namespace mm
