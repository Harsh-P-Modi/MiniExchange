#include "apps/benchmark/latency_bench.hpp"

#include <chrono>
#include <cstddef>

#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/event_sink.hpp"

namespace miniexchange::benchmark {
namespace {
// The latency benchmarks build at most ~100 resting orders per iteration
// and construct a fresh engine each time. The production 1,000,000-slot
// default pool would `::operator new` ~72 MB and then strided-write every
// one of its pages (free-list init) on EACH iteration -- with tens of
// thousands of iterations that is minutes of page-fault churn on Linux
// (it was the cause of `benchmark_harness` appearing to hang). A small
// pool is functionally identical for what these micro-benchmarks measure:
// a single timed submit()/cancel() whose cost does not depend on pool
// size. Must exceed the deepest per-iteration book (~101 orders).
constexpr std::size_t kBenchPoolCapacity = 4096;
}  // namespace

void bench_add_no_match(LatencyRecorder& recorder, std::size_t iterations) {
    for (std::size_t i = 0; i < iterations; ++i) {
        // Fresh engine per iteration — construction is deliberately untimed.
        // No resting liquidity, so the single ADD cannot cross.
        MatchingEngine engine{NullEventSink::instance(), kBenchPoolCapacity};

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
        MatchingEngine engine{NullEventSink::instance(), kBenchPoolCapacity};

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
        MatchingEngine engine{NullEventSink::instance(), kBenchPoolCapacity};

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
