#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace mm {

Config Config::load(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) {
        throw ConfigError("Cannot open config file: " + filepath);
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw ConfigError(std::string("JSON parse error: ") + e.what());
    }

    Config cfg;

    // Required fields
    auto require = [&](const char* key) {
        if (!j.contains(key)) {
            throw ConfigError(std::string("Missing required field: ") + key);
        }
    };

    require("api_key");
    require("api_secret");
    require("private_key");
    require("market_token_id");

    cfg.api_key = j["api_key"].get<std::string>();
    cfg.api_secret = j["api_secret"].get<std::string>();
    cfg.private_key = j["private_key"].get<std::string>();
    cfg.market_token_id = j["market_token_id"].get<std::string>();

    // Optional fields with defaults
    if (j.contains("spread")) cfg.spread = j["spread"].get<double>();
    if (j.contains("order_size")) cfg.order_size = j["order_size"].get<double>();
    if (j.contains("poll_interval_ms")) cfg.poll_interval_ms = j["poll_interval_ms"].get<int>();
    if (j.contains("requote_threshold")) cfg.requote_threshold = j["requote_threshold"].get<double>();
    if (j.contains("log_file")) cfg.log_file = j["log_file"].get<std::string>();
    if (j.contains("pnl_report_interval_s")) cfg.pnl_report_interval_s = j["pnl_report_interval_s"].get<int>();

    cfg.validate();
    return cfg;
}

void Config::validate() const {
    if (spread <= 0.0) {
        throw ConfigError("spread must be positive, got: " + std::to_string(spread));
    }
    if (spread > 0.1) {
        throw ConfigError("spread too large (max 0.1), got: " + std::to_string(spread));
    }
    if (order_size <= 0.0) {
        throw ConfigError("order_size must be positive");
    }
    if (poll_interval_ms <= 0) {
        throw ConfigError("poll_interval_ms must be positive");
    }
    if (api_key.empty()) {
        throw ConfigError("Missing required field: api_key");
    }
    if (api_secret.empty()) {
        throw ConfigError("Missing required field: api_secret");
    }
    if (private_key.empty()) {
        throw ConfigError("Missing required field: private_key");
    }
    if (market_token_id.empty()) {
        throw ConfigError("Missing required field: market_token_id");
    }
}

} // namespace mm
