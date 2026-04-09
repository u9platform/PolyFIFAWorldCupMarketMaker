#include "config.h"
#include "market_maker.h"
#include "real_api_client.h"
#include "order_book.h"
#include "quote_engine.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

static std::atomic<bool> g_running{true};
static mm::MarketMaker* g_mm = nullptr;

void signalHandler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    g_running = false;
    if (g_mm) {
        g_mm->stop();
    }
}

// Monitor mode: read-only, just watch the order book
void runMonitor(const std::string& token_id, int poll_ms, double spread) {
    mm::RealApiClient api;

    spdlog::info("Monitor mode: watching token {}...", token_id.substr(0, 20) + "...");
    spdlog::info("Poll interval: {}ms, configured spread: {}", poll_ms, spread);

    int tick = 0;
    while (g_running) {
        try {
            auto ob_json = api.getOrderBook(token_id);
            mm::OrderBook ob;
            ob.parse(ob_json);

            if (!ob.isValid()) {
                spdlog::warn("[tick {}] Invalid order book", tick);
            } else {
                double mid = ob.midPrice();
                double micro = ob.microPrice();
                double mkt_spread = ob.bestAsk() - ob.bestBid();

                auto quote = mm::QuoteEngine::calculateQuotes(mid, spread);
                std::string quote_str = "N/A";
                if (quote) {
                    quote_str = fmt::format("{:.4f}/{:.4f}", quote->bid_price, quote->ask_price);
                }

                spdlog::info("[tick {:>4}] bid={:.4f} x{:<10.0f} | ask={:.4f} x{:<10.0f} | "
                             "mid={:.4f} micro={:.4f} spread={:.4f} ({:.1f}%) | "
                             "our quote={}",
                             tick,
                             ob.bestBid(), ob.bestBidSize(),
                             ob.bestAsk(), ob.bestAskSize(),
                             mid, micro, mkt_spread,
                             mkt_spread / mid * 100,
                             quote_str);
            }
        } catch (const std::exception& e) {
            spdlog::error("[tick {}] Error: {}", tick, e.what());
        }

        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }

    spdlog::info("Monitor stopped after {} ticks", tick);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  mm_bot <config.json>              # Full market maker mode" << std::endl;
        std::cerr << "  mm_bot --monitor <token_id>       # Monitor order book only" << std::endl;
        std::cerr << "  mm_bot --monitor <token_id> <poll_ms> <spread>" << std::endl;
        return 1;
    }

    // Setup logging
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("mm", spdlog::sinks_init_list{console_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string arg1(argv[1]);

    if (arg1 == "--monitor") {
        if (argc < 3) {
            std::cerr << "Usage: mm_bot --monitor <token_id> [poll_ms] [spread]" << std::endl;
            return 1;
        }
        std::string token_id = argv[2];
        int poll_ms = argc >= 4 ? std::stoi(argv[3]) : 5000;
        double spread = argc >= 5 ? std::stod(argv[4]) : 0.002;
        runMonitor(token_id, poll_ms, spread);
        return 0;
    }

    // Full market maker mode
    try {
        auto cfg = mm::Config::load(arg1);

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(cfg.log_file, true);
        logger->sinks().push_back(file_sink);

        spdlog::info("Config loaded: token={} spread={} size={} poll={}ms",
                     cfg.market_token_id, cfg.spread, cfg.order_size, cfg.poll_interval_ms);

        mm::RealApiClient api(cfg.api_key, cfg.api_secret, cfg.private_key);
        mm::MarketMaker maker(cfg, api);
        g_mm = &maker;

        maker.start();
    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        return 1;
    }

    return 0;
}
