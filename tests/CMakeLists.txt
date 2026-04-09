FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Helper: add a test executable linked to mm_core + gtest
function(add_mm_test TEST_NAME TEST_SRC)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
    target_link_libraries(${TEST_NAME} PRIVATE mm_core GTest::gtest_main GTest::gmock)
    gtest_discover_tests(${TEST_NAME})
endfunction()

# Unit tests - each module is an independent binary
add_mm_test(test_quote_engine      unit/test_quote_engine.cpp)
add_mm_test(test_order_book        unit/test_order_book.cpp)
add_mm_test(test_position_tracker  unit/test_position_tracker.cpp)
add_mm_test(test_pnl_reporter      unit/test_pnl_reporter.cpp)
add_mm_test(test_config            unit/test_config.cpp)

# Integration tests
add_mm_test(test_order_manager     integration/test_order_manager.cpp)
add_mm_test(test_market_maker      integration/test_market_maker.cpp)
