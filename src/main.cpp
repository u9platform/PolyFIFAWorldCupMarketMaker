#include "config.h"
#include "market_maker.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <csignal>
#include <iostream>
#include <memory>

static mm::MarketMaker* g_mm = nullptr;

void signalHandler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    if (g_mm) {
        g_mm->stop();
    }
}

// Placeholder API client - real implementation connects to Polymarket CLOB
class RealApiClient : public mm::IApiClient {
public:
    nlohmann::json getOrderBook(const std::string&) override {
        // TODO: implement HTTP call to clob.polymarket.com
        throw mm::ApiError("Not implemented");
    }
    std::string placeOrder(const std::string&, mm::Side, double, double) override {
        throw mm::ApiError("Not implemented");
    }
    void cancelOrder(const std::string&) override {
        throw mm::ApiError("Not implemented");
    }
    mm::OrderStatus getOrderStatus(const std::string&) override {
        throw mm::ApiError("Not implemented");
    }
    double getFilledQty(const std::string&) override {
        throw mm::ApiError("Not implemented");
    }
    double getBalance() override {
        throw mm::ApiError("Not implemented");
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: mm_bot <config.json>" << std::endl;
        return 1;
    }

    try {
        auto cfg = mm::Config::load(argv[1]);

        // Setup logging
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(cfg.log_file, true);
        auto logger = std::make_shared<spdlog::logger>("mm",
            spdlog::sinks_init_list{console_sink, file_sink});
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);

        spdlog::info("Config loaded: token={} spread={} size={} poll={}ms",
                     cfg.market_token_id, cfg.spread, cfg.order_size, cfg.poll_interval_ms);

        RealApiClient api;
        mm::MarketMaker maker(cfg, api);
        g_mm = &maker;

        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        maker.start();
    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        return 1;
    }

    return 0;
}
