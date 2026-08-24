cmake_minimum_required(VERSION 3.14)
project(MiniExchange VERSION 0.1.0 LANGUAGES CXX)

# C++20 standard, required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to RelWithDebInfo if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Compiler warnings as errors — applied to our targets only (not to
# third-party FetchContent dependencies, which have their own warning
# levels and may not compile cleanly under -Werror).
# We use an INTERFACE library so each target can link it rather than
# using add_compile_options() globally.
add_library(miniexchange_warnings INTERFACE)
if(MSVC)
    target_compile_options(miniexchange_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(miniexchange_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Werror)
endif()

# Enable testing
enable_testing()

# FetchContent for dependencies
include(FetchContent)

# GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google Benchmark
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
# Disable benchmark's own -Werror to avoid third-party build failures
# on newer compilers (e.g. GCC 14 flags CacheUnknown unhandled in switch).
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# Source root on the include path — applied via the warnings interface
# library so all our targets pick it up without polluting third-party targets.
target_include_directories(miniexchange_warnings INTERFACE ${CMAKE_SOURCE_DIR})

# Libraries
add_library(orderbook orderbook/price_level.cpp orderbook/order_book.cpp)
target_link_libraries(orderbook PRIVATE miniexchange_warnings)

add_library(engine engine/matching_engine.cpp)
target_link_libraries(engine PUBLIC orderbook PRIVATE miniexchange_warnings)

# Tests
add_executable(core_types_test tests/core_types_test.cpp)
target_link_libraries(core_types_test PRIVATE GTest::gtest_main miniexchange_warnings)

add_executable(core_trade_test tests/core_trade_test.cpp)
target_link_libraries(core_trade_test PRIVATE GTest::gtest_main miniexchange_warnings)

add_executable(order_types_test tests/test_order_types.cpp)
target_link_libraries(order_types_test PRIVATE GTest::gtest_main miniexchange_warnings)

add_executable(events_test tests/test_events.cpp)
target_link_libraries(events_test PRIVATE GTest::gtest_main miniexchange_warnings)

add_executable(interfaces_test tests/test_interfaces.cpp)
target_link_libraries(interfaces_test PRIVATE GTest::gtest_main miniexchange_warnings)

add_executable(price_level_test tests/price_level_test.cpp)
target_link_libraries(price_level_test PRIVATE GTest::gtest_main orderbook miniexchange_warnings)

add_executable(order_book_test tests/order_book_test.cpp)
target_link_libraries(order_book_test PRIVATE GTest::gtest_main orderbook miniexchange_warnings)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(core_types_test)
gtest_discover_tests(core_trade_test)
gtest_discover_tests(order_types_test)
gtest_discover_tests(events_test)
gtest_discover_tests(interfaces_test)
gtest_discover_tests(price_level_test)
gtest_discover_tests(order_book_test)

add_executable(matching_engine_test tests/matching_engine_test.cpp)
target_link_libraries(matching_engine_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(matching_engine_test)

add_executable(integration_test tests/integration_test.cpp)
target_link_libraries(integration_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(integration_test)

add_executable(edge_case_test tests/edge_case_test.cpp)
target_link_libraries(edge_case_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(edge_case_test)

# CLI application
add_executable(mini_exchange
    apps/cli/main.cpp
    apps/cli/cli_parser.cpp
    apps/cli/console_printer.cpp)
target_link_libraries(mini_exchange PRIVATE engine miniexchange_warnings)
