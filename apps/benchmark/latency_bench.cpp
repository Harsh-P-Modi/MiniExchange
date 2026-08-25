#include "apps/benchmark/latency_bench.hpp"

#include <chrono>

#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"

namespace miniexchange::benchmark {

void bench_add_no_match(LatencyRecorder& recorder, std::size_t iterations) {
    for (std::size_t i = 0; i < iterations; ++i) {
        // Fresh engine per iteration — construction is deliberately untimed.
        // No resting liquidity, so the single ADD cannot cross.
        MatchingEngine engine;

        LimitOrder order{OrderId{1}, Side::Buy, Price{10000}, Quantity{100}};

        auto start = std::chrono::steady_clock::now();
        engine.submit(NewOrder{order});
        auto end = std::chrono::steady_clock::now();

        recorder.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
}

void bench_add_with_match(LatencyRecorder& recorder, std::size_t iterations,
                          std::size_t fill_count) {
    for (std::size_t i = 0; i < iterations; ++i) {
        // Fresh engine per iteration — construction is deliberately untimed.
        MatchingEngine engine;

        // Untimed setup: insert fill_count resting sell orders at ascending
        // prices starting at 10000. Each has quantity 10.
        for (std::size_t j = 0; j < fill_count; ++j) {
            LimitOrder resting{OrderId{j + 1}, Side::Sell,
                               Price{static_cast<int64_t>(10000 + j)},
                               Quantity{10}};
            engine.submit(NewOrder{resting});
        }

        // The one order being measured: a buy whose price crosses all
        // resting sells, with enough quantity to consume them all.
        LimitOrder aggressive{
            OrderId{fill_count + 1}, Side::Buy,
            Price{static_cast<int64_t>(10000 + fill_count - 1)},
            Quantity{static_cast<uint64_t>(10 * fill_count)}};

        auto start = std::chrono::steady_clock::now();
        engine.submit(NewOrder{aggressive});
        auto end = std::chrono::steady_clock::now();

        recorder.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
}

void bench_cancel(LatencyRecorder& recorder, std::size_t iterations,
                  bool front_of_queue) {
    for (std::size_t i = 0; i < iterations; ++i) {
        // Fresh engine per iteration — construction is deliberately untimed.
        MatchingEngine engine;

        // Untimed setup: insert several orders at the same price level
        // to create a meaningful queue depth.
        constexpr std::size_t kQueueDepth = 100;
        for (std::size_t j = 0; j < kQueueDepth; ++j) {
            LimitOrder order{OrderId{j + 1}, Side::Buy, Price{10000},
                             Quantity{10}};
            engine.submit(NewOrder{order});
        }

        // The order to cancel: front (OrderId{1}) or back (OrderId{kQueueDepth})
        OrderId cancel_id =
            front_of_queue ? OrderId{1} : OrderId{kQueueDepth};

        auto start = std::chrono::steady_clock::now();
        engine.cancel(cancel_id);
        auto end = std::chrono::steady_clock::now();

        recorder.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
}

} // namespace miniexchange::benchmark
