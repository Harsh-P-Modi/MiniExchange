// Phase 2, Task 7 — BM_SustainedThroughput (R4)
//
// Pre-generates 100k events via WorkloadGenerator outside the timed
// region (NFR2), then runs a fresh engine per repetition. Reports
// orders/sec via Google Benchmark's SetItemsProcessed.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <variant>

#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "tools/workload_generator/workload_generator.hpp"

namespace {

using namespace miniexchange;

static void BM_SustainedThroughput(benchmark::State& state) {
    // Pre-generate workload OUTSIDE the timed region (NFR2).
    WorkloadConfig config{
        .seed = 12345,
        .mid_price = Price{10000},
        .price_stddev_log = 0.3,
        .quantity_min = Quantity{1},
        .quantity_max = Quantity{100},
        .add_limit_ratio = 0.6,
        .add_market_ratio = 0.1,
        .cancel_ratio = 0.3,
    };
    WorkloadGenerator gen(config);
    auto events = gen.generate(100'000);

    // Pool sized to the bounded workload (0.3 cancel ratio keeps the book
    // well under 128k), not the 1,000,000-slot production default whose
    // 72 MB alloc + page-touch per repetition is pure benchmark overhead.
    constexpr std::size_t kThroughputPoolCapacity = 1u << 18;  // 262144

    for (auto _ : state) {
        MatchingEngine engine{NullEventSink::instance(),
                              kThroughputPoolCapacity};  // fresh per repetition
        for (const auto& event : events) {
            std::visit(
                [&](const auto& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, LimitOrder>) {
                        engine.submit(NewOrder{e});
                    } else if constexpr (std::is_same_v<T, MarketOrder>) {
                        engine.submit(NewOrder{e});
                    } else if constexpr (std::is_same_v<T, CancelRequest>) {
                        engine.cancel(e.id);
                    }
                },
                event);
        }
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(events.size()));
}

BENCHMARK(BM_SustainedThroughput)->UseRealTime();

}  // namespace
