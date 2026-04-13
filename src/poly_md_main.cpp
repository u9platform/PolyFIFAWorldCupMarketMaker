#include "market_data_writer.h"
#include "market_data_reader.h"
#include "ws_market_feed.h"
#include "types.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <csignal>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  poly_md --tokens <t1>,<t2>,... [--shm-name /poly_md]" << std::endl;
        std::cerr << "  poly_md --monitor [--shm-name /poly_md]" << std::endl;
        return 1;
    }

    auto logger = spdlog::stdout_color_mt("poly_md");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string arg1(argv[1]);
    std::string shm_name = "/poly_md";

    // Find --shm-name
    for (int i = 2; i < argc - 1; i++) {
        if (std::string(argv[i]) == "--shm-name") {
            shm_name = argv[i + 1];
        }
    }

    if (arg1 == "--monitor") {
        // Read-only monitor mode
        try {
            mm::MarketDataReader reader(shm_name);
            spdlog::info("Monitor: connected to SHM {}, {} slots", shm_name, reader.numSlots());

            while (g_running) {
                if (!reader.isAlive()) {
                    spdlog::warn("Writer not alive!");
                }
                // Print all non-empty slots
                for (uint32_t i = 0; i < reader.numSlots(); i++) {
                    mm::MarketSlot slot;
                    // Read by iterating slots directly (need token_hash)
                    // For monitor, just show summary
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        } catch (const std::exception& e) {
            spdlog::error("Monitor failed: {}", e.what());
            return 1;
        }
        return 0;
    }

    if (arg1 == "--tokens") {
        if (argc < 3) {
            std::cerr << "Missing token list" << std::endl;
            return 1;
        }

        // Parse comma-separated tokens
        std::vector<std::string> tokens;
        std::string tok_arg(argv[2]);
        size_t pos = 0;
        while ((pos = tok_arg.find(',')) != std::string::npos) {
            tokens.push_back(tok_arg.substr(0, pos));
            tok_arg.erase(0, pos + 1);
        }
        tokens.push_back(tok_arg);

        spdlog::info("poly_md starting: {} tokens, shm={}", tokens.size(), shm_name);

        // Create shared memory writer
        mm::MarketDataWriter writer(shm_name, mm::MAX_MARKETS);
        for (auto& t : tokens) {
            writer.registerToken(t);
        }

        // Start WebSocket feed
        mm::WsMarketFeed feed(tokens);

        feed.onBook([&](const std::string& asset_id, const mm::OrderBook& ob) {
            if (!ob.isValid()) return;
            writer.updateQuote(asset_id, ob.bestBid(), ob.bestAsk(),
                               mm::nowMs());

            // Update depth
            auto& bids = ob.bids();
            auto& asks = ob.asks();
            double bp[5]={}, bs[5]={}, ap[5]={}, as[5]={};
            int n = std::min(5, static_cast<int>(bids.size()));
            for (int i = 0; i < n; i++) { bp[i] = bids[i].price; bs[i] = bids[i].size; }
            n = std::min(5, static_cast<int>(asks.size()));
            for (int i = 0; i < n; i++) { ap[i] = asks[i].price; as[i] = asks[i].size; }
            writer.updateDepth(asset_id, bp, bs, ap, as, 5);
        });

        feed.onPriceChange([&](const std::string& asset_id, const mm::BestQuote& q) {
            writer.updateQuote(asset_id, q.best_bid, q.best_ask, q.timestamp_ms);
        });

        feed.onTrade([&](const std::string& asset_id, mm::Side side, double price, double size) {
            writer.updateTrade(asset_id, price,
                               side == mm::Side::BUY ? 1 : 2,
                               size, mm::nowMs());
        });

        feed.start();
        spdlog::info("WebSocket feed started");

        // Heartbeat loop
        while (g_running) {
            writer.heartbeat();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        feed.stop();
        spdlog::info("poly_md stopped");
        return 0;
    }

    std::cerr << "Unknown argument: " << arg1 << std::endl;
    return 1;
}
