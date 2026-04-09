cmake_minimum_required(VERSION 3.16)
project(PolyFIFAWorldCupMarketMaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)

FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
)
set(USE_TLS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json spdlog ixwebsocket)

# System dependencies
find_package(CURL REQUIRED)

# Main library (all modules, linked by tests and main)
add_library(mm_core STATIC
    src/quote_engine.cpp
    src/order_book.cpp
    src/position_tracker.cpp
    src/pnl_reporter.cpp
    src/config.cpp
    src/order_manager.cpp
    src/market_maker.cpp
    src/real_api_client.cpp
    src/ws_market_feed.cpp
)

target_include_directories(mm_core PUBLIC src)
target_link_libraries(mm_core PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog CURL::libcurl ixwebsocket::ixwebsocket)

# Main executable
add_executable(mm_bot src/main.cpp)
target_link_libraries(mm_bot PRIVATE mm_core)

# Tests
enable_testing()
add_subdirectory(tests)
