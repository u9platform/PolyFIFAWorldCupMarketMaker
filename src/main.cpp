#include "config.h"
#include "market_maker.h"
#include "real_api_client.h"
#include "order_book.h"
#include "quote_engine.h"
#include "ws_market_feed.h"
#include "dryrun_api_client.h"
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
        std::cerr << "  mm_bot --monitor <token_id>       # Monitor order book (HTTP poll)" << std::endl;
        std::cerr << "  mm_bot --ws-monitor <token_id>    # Monitor order book (WebSocket)" << std::endl;
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

    if (arg1 == "--ws-monitor") {
        if (argc < 3) {
            std::cerr << "Usage: mm_bot --ws-monitor <token_id>" << std::endl;
            return 1;
        }
        std::string token_id = argv[2];

        mm::WsMarketFeed feed(token_id);
        int msg_count = 0;

        feed.onBook([&](const std::string& asset_id, const mm::OrderBook& ob) {
            spdlog::info("[BOOK {}...] bid={:.4f} x{:.0f} | ask={:.4f} x{:.0f} | mid={:.4f}",
                         asset_id.substr(0, 8),
                         ob.bestBid(), ob.bestBidSize(),
                         ob.bestAsk(), ob.bestAskSize(),
                         ob.midPrice());
            msg_count++;
        });

        feed.onPriceChange([&](const std::string& asset_id, const mm::BestQuote& q) {
            int64_t now = mm::nowMs();
            int64_t latency = now - q.timestamp_ms;
            spdlog::info("[PRICE {}...] bid={:.4f} ask={:.4f} spread={:.4f} latency={}ms",
                         asset_id.substr(0, 8),
                         q.best_bid, q.best_ask,
                         q.best_ask - q.best_bid, latency);
            msg_count++;
        });

        feed.onTrade([&](const std::string& asset_id, mm::Side side, double price, double size) {
            spdlog::info("[TRADE {}...] {} {:.4f} x{:.2f}",
                         asset_id.substr(0, 8),
                         side == mm::Side::BUY ? "BUY" : "SELL", price, size);
            msg_count++;
        });

        feed.start();
        spdlog::info("WebSocket monitor started, press Ctrl+C to stop");

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        feed.stop();
        spdlog::info("Received {} messages total", msg_count);
        return 0;
    }

    if (arg1 == "--dryrun") {
        if (argc < 3) {
            std::cerr << "Usage: mm_bot --dryrun <token_id>[,token_id2,...] [size] [poll_ms] [gamma]" << std::endl;
            return 1;
        }

        // Parse comma-separated token IDs
        std::vector<std::string> token_ids;
        std::string tokens_arg = argv[2];
        size_t pos = 0;
        while ((pos = tokens_arg.find(',')) != std::string::npos) {
            token_ids.push_back(tokens_arg.substr(0, pos));
            tokens_arg.erase(0, pos + 1);
        }
        token_ids.push_back(tokens_arg);

        double size = argc >= 4 ? std::stod(argv[3]) : 100;
        int poll_ms = argc >= 5 ? std::stoi(argv[4]) : 5000;
        double gamma = argc >= 6 ? std::stod(argv[5]) : 0.1;

        spdlog::info("=== DRY RUN MODE ({} markets, AS model) ===", token_ids.size());
        for (auto& t : token_ids) {
            spdlog::info("  Token: {}...", t.substr(0, 20));
        }
        spdlog::info("Size: {}  Poll: {}ms  Gamma: {}", size, poll_ms, gamma);

        mm::DryRunApiClient api;
        mm::Config cfg;
        cfg.market_token_ids = token_ids;
        cfg.order_size = size;
        cfg.poll_interval_ms = poll_ms;
        cfg.requote_threshold = 0.001;
        cfg.gamma = gamma;
        cfg.min_spread = 0.001;
        cfg.max_inventory = 1000;
        cfg.vol_window_size = 100;
        cfg.expiry_date = "2026-07-20";
        cfg.api_key = "dryrun";
        cfg.api_secret = "dryrun";
        cfg.private_key = "dryrun";

        mm::MarketMaker maker(cfg, api);
        g_mm = &maker;

        // Single WS connection for all markets
        mm::WsMarketFeed feed(token_ids);
        feed.onPriceChange([&api](const std::string& asset_id, const mm::BestQuote& q) {
            api.simulateFills(asset_id, q.best_bid, q.best_ask);
        });
        feed.start();

        maker.start();

        feed.stop();
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
