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

# nlohmann/json — Phase 7's first non-test/benchmark external dependency.
# Scoped strictly to the JSON codec and protocol benchmark targets; never
# linked into core/, engine/, orderbook/, or the binary_protocol adapter.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
# Header-only library — no build step, just makes the target available.
FetchContent_MakeAvailable(nlohmann_json)

# Source root on the include path — applied via the warnings interface
# library so all our targets pick it up without polluting third-party targets.
target_include_directories(miniexchange_warnings INTERFACE ${CMAKE_SOURCE_DIR})

# Libraries
add_library(orderbook orderbook/price_level.cpp orderbook/order_book.cpp orderbook/order_pool.cpp)
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

add_executable(order_pool_test tests/order_pool_test.cpp)
target_link_libraries(order_pool_test PRIVATE GTest::gtest_main orderbook miniexchange_warnings)
gtest_discover_tests(order_pool_test)

add_executable(matching_engine_test tests/matching_engine_test.cpp)
target_link_libraries(matching_engine_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(matching_engine_test)

add_executable(integration_test tests/integration_test.cpp)
target_link_libraries(integration_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(integration_test)

add_executable(edge_case_test tests/edge_case_test.cpp)
target_link_libraries(edge_case_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(edge_case_test)

add_executable(pool_exhaustion_test tests/pool_exhaustion_test.cpp)
target_link_libraries(pool_exhaustion_test PRIVATE GTest::gtest_main engine miniexchange_warnings)
gtest_discover_tests(pool_exhaustion_test)

# Shared text protocol library (extracted from apps/cli/ for reuse by adapters/tcp/)
add_library(text_protocol
    adapters/text_protocol/text_protocol_parser.cpp
    adapters/text_protocol/text_protocol_renderer.cpp)
target_link_libraries(text_protocol PRIVATE miniexchange_warnings)

# UDP feed adapter library (Phase 6)
add_library(adapters_udp
    adapters/udp/udp_feed_publisher.cpp
    adapters/udp/book_builder.cpp)
target_link_libraries(adapters_udp PRIVATE miniexchange_warnings)

# Phase 6: UDP feed message struct layout + SymbolId hash tests
add_executable(udp_feed_message_test tests/udp_feed_message_test.cpp)
target_link_libraries(udp_feed_message_test PRIVATE
    GTest::gtest_main adapters_udp miniexchange_warnings)
gtest_discover_tests(udp_feed_message_test)

# Phase 6: UDP feed publisher tests (tasks 7–13)
add_executable(udp_publisher_test tests/udp_publisher_test.cpp)
target_link_libraries(udp_publisher_test PRIVATE
    GTest::gtest_main adapters_udp miniexchange_warnings)
gtest_discover_tests(udp_publisher_test)

# Phase 6: Publisher end-to-end test against real MatchingEngine (task 14)
add_executable(udp_publisher_e2e_test tests/udp_publisher_e2e_test.cpp)
target_link_libraries(udp_publisher_e2e_test PRIVATE
    GTest::gtest_main engine adapters_udp miniexchange_warnings)
gtest_discover_tests(udp_publisher_e2e_test)

# Phase 6: BookBuilder unit tests (tasks 15–19)
add_executable(udp_book_builder_test tests/udp_book_builder_test.cpp)
target_link_libraries(udp_book_builder_test PRIVATE
    GTest::gtest_main adapters_udp miniexchange_warnings)
gtest_discover_tests(udp_book_builder_test)

# Phase 6: End-to-end Definition of Done tests (tasks 20–21)
add_executable(udp_e2e_dod_test tests/udp_e2e_dod_test.cpp)
target_link_libraries(udp_e2e_dod_test PRIVATE
    GTest::gtest_main engine adapters_udp miniexchange_warnings)
gtest_discover_tests(udp_e2e_dod_test)

# TCP adapter library (Linux only — epoll-based, nonblocking)
if(UNIX AND NOT APPLE)
    add_library(adapters_tcp
        adapters/tcp/tcp_server.cpp)
    target_link_libraries(adapters_tcp PRIVATE miniexchange_warnings)

    # TCP server integration tests (requires real sockets + epoll)
    add_executable(tcp_server_test tests/tcp_server_test.cpp)
    target_link_libraries(tcp_server_test PRIVATE
        GTest::gtest_main adapters_tcp miniexchange_warnings)
    gtest_discover_tests(tcp_server_test)

    # Exchange server app — multi-threaded TCP composition root (Phase 5)
    add_executable(exchange_server apps/exchange_server/main.cpp)
    target_link_libraries(exchange_server PRIVATE
        engine adapters_tcp adapters_udp text_protocol miniexchange_warnings)

    # Exchange server E2E integration test (full round-trip through real sockets)
    add_executable(exchange_server_e2e_test tests/exchange_server_e2e_test.cpp)
    target_link_libraries(exchange_server_e2e_test PRIVATE
        GTest::gtest_main engine adapters_tcp text_protocol miniexchange_warnings)
    gtest_discover_tests(exchange_server_e2e_test)

    # Phase 5: TCP round-trip latency benchmark (NOT registered with ctest; run manually)
    add_executable(tcp_roundtrip_bench
        apps/benchmark/tcp_roundtrip_bench.cpp
        apps/benchmark/latency_recorder.cpp)
    target_link_libraries(tcp_roundtrip_bench PRIVATE
        engine adapters_tcp text_protocol miniexchange_warnings)

    # --- Phase 7: Binary protocol ---

    # Binary protocol adapter library (Phase 7)
    add_library(adapters_binary_protocol
        adapters/binary_protocol/BinaryCodec.cpp)
    target_link_libraries(adapters_binary_protocol PRIVATE miniexchange_warnings)

    # Phase 7: Byte-order helpers unit tests
    add_executable(binary_byte_order_test tests/binary_byte_order_test.cpp)
    target_link_libraries(binary_byte_order_test PRIVATE
        GTest::gtest_main miniexchange_warnings)
    gtest_discover_tests(binary_byte_order_test)

    # Phase 7: Binary codec encode/decode tests (tasks 4-7)
    add_executable(binary_codec_test tests/binary_codec_test.cpp)
    target_link_libraries(binary_codec_test PRIVATE
        GTest::gtest_main adapters_binary_protocol miniexchange_warnings)
    gtest_discover_tests(binary_codec_test)
endif()

# Tools libraries
add_library(workload_generator tools/workload_generator/workload_generator.cpp)
target_link_libraries(workload_generator PRIVATE miniexchange_warnings)

# Workload generator test
add_executable(workload_generator_test tests/workload_generator_test.cpp)
target_link_libraries(workload_generator_test PRIVATE
    GTest::gtest_main workload_generator miniexchange_warnings)
gtest_discover_tests(workload_generator_test)

# CLI application
add_executable(mini_exchange
    apps/cli/main.cpp
    apps/cli/cli_parser.cpp
    apps/cli/console_printer.cpp)
target_link_libraries(mini_exchange PRIVATE engine text_protocol miniexchange_warnings)

# Latency recorder test (Phase 2, Task 3) — latency_recorder.cpp is app-local,
# compiled directly into the test executable rather than a shared library.
add_executable(latency_recorder_test
    tests/latency_recorder_test.cpp
    apps/benchmark/latency_recorder.cpp)
target_link_libraries(latency_recorder_test PRIVATE
    GTest::gtest_main miniexchange_warnings)
gtest_discover_tests(latency_recorder_test)

# Phase 4: EngineCommand variant test (header-only, no library needed)
add_executable(engine_command_test tests/engine_command_test.cpp)
target_link_libraries(engine_command_test PRIVATE GTest::gtest_main miniexchange_warnings)
gtest_discover_tests(engine_command_test)

# Phase 4: SpscRingBuffer single-threaded correctness tests
add_executable(spsc_ring_buffer_test tests/spsc_ring_buffer_test.cpp)
target_link_libraries(spsc_ring_buffer_test PRIVATE GTest::gtest_main miniexchange_warnings)
gtest_discover_tests(spsc_ring_buffer_test)

# Phase 4: SpscRingBuffer concurrent stress test (run under TSan in CI)
add_executable(spsc_stress_test tests/spsc_stress_test.cpp)
target_link_libraries(spsc_stress_test PRIVATE GTest::gtest_main miniexchange_warnings)
gtest_discover_tests(spsc_stress_test)

# Phase 5: Inbound queue integration test (framing, parse→push, compile verification)
add_executable(inbound_queue_test tests/inbound_queue_test.cpp)
target_link_libraries(inbound_queue_test PRIVATE
    GTest::gtest_main text_protocol miniexchange_warnings)
gtest_discover_tests(inbound_queue_test)

# Phase 5: Response routing integration test (multiple clients → engine → per-client responses)
add_executable(response_routing_test tests/response_routing_test.cpp)
target_link_libraries(response_routing_test PRIVATE
    GTest::gtest_main engine text_protocol miniexchange_warnings)
gtest_discover_tests(response_routing_test)

# Phase 5: R8 spin-retry path test (outbound queue full → spin until space freed)
add_executable(r8_spin_test tests/r8_spin_test.cpp)
target_link_libraries(r8_spin_test PRIVATE GTest::gtest_main miniexchange_warnings)
gtest_discover_tests(r8_spin_test)

# Benchmark harness (Phase 2) — NOT registered with ctest; run manually.
add_executable(benchmark_harness
    apps/benchmark/main.cpp
    apps/benchmark/latency_bench.cpp
    apps/benchmark/latency_recorder.cpp
    apps/benchmark/results_writer.cpp
    apps/benchmark/throughput_bench.cpp)
target_link_libraries(benchmark_harness PRIVATE
    benchmark::benchmark
    engine orderbook workload_generator miniexchange_warnings)

# Phase 4: Queue comparison benchmark (SpscRingBuffer vs MutexQueue)
# NOT registered with ctest; run manually.
add_executable(queue_benchmark
    apps/benchmark/queue_bench_main.cpp
    apps/benchmark/queue_bench.cpp
    apps/benchmark/latency_recorder.cpp)
target_link_libraries(queue_benchmark PRIVATE
    workload_generator miniexchange_warnings)
