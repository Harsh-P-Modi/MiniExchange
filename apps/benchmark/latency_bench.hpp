#ifndef MINIEXCHANGE_APPS_BENCHMARK_LATENCY_BENCH_HPP
#define MINIEXCHANGE_APPS_BENCHMARK_LATENCY_BENCH_HPP

#include <cstddef>

#include "apps/benchmark/latency_recorder.hpp"

namespace miniexchange::benchmark {

/// bench_add_no_match (R1): measures the latency of a single non-crossing
/// limit order ADD into a fresh engine with no resting liquidity.
/// A fresh MatchingEngine is constructed per iteration (untimed).
void bench_add_no_match(LatencyRecorder& recorder, std::size_t iterations);

/// bench_add_with_match (R2): measures the latency of a single aggressive
/// order that crosses and fills `fill_count` resting orders.
/// Untimed setup inserts the resting liquidity; only the crossing submit is timed.
void bench_add_with_match(LatencyRecorder& recorder, std::size_t iterations,
                          std::size_t fill_count);

/// bench_cancel (R3): measures the latency of cancelling a single order from
/// a queue of 100 orders at the same price level.
/// @param front_of_queue  If true, cancel the first order inserted (front);
///                        if false, cancel the last (back).
/// Both should be O(1) due to intrusive-list unlink + hash-map erase.
void bench_cancel(LatencyRecorder& recorder, std::size_t iterations,
                  bool front_of_queue);

} // namespace miniexchange::benchmark

#endif // MINIEXCHANGE_APPS_BENCHMARK_LATENCY_BENCH_HPP
