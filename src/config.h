#pragma once

#include <string>
#include <stdexcept>

namespace mm {

class ConfigError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Config {
    std::string market_token_id;
    double spread = 0.002;
    double order_size = 100.0;
    int poll_interval_ms = 10000;
    double requote_threshold = 0.001;
    std::string api_key;
    std::string api_secret;
    std::string private_key;
    std::string log_file = "mm.log";
    int pnl_report_interval_s = 60;

    // Load from JSON file. Throws ConfigError on invalid/missing fields.
    static Config load(const std::string& filepath);

    // Validate loaded config. Throws ConfigError on invalid values.
    void validate() const;
};

} // namespace mm
