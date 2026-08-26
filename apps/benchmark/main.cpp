// Benchmark harness entry point (Phase 2 design, updated for Phase 3).
//
// Custom main() that:
// 1. Runs latency benchmarks (Tasks 4-6) via LatencyRecorder
// 2. Runs a manual throughput measurement for the results file
// 3. Writes results to benchmarks/results/phase-03-pooled.md with
//    Phase 2 baseline comparison table
// 4. Calls benchmark::RunSpecifiedBenchmarks() for Google Benchmark output

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <variant>
#include <vector>

#include <benchmark/benchmark.h>

#include "apps/benchmark/latency_bench.hpp"
#include "apps/benchmark/latency_recorder.hpp"
#include "apps/benchmark/results_writer.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "tools/workload_generator/workload_generator.hpp"

namespace {

constexpr std::size_t kLatencyIterations = 10000;
constexpr std::size_t kThroughputEvents = 100'000;
constexpr std::size_t kThroughputRepetitions = 10;

void print_recorder(const char* label,
                    const miniexchange::benchmark::LatencyRecorder& recorder) {
    std::printf("  %-25s  avg=%8.1f  median=%8.1f  P99=%8.1f  max=%8.1f ns\n",
                label, recorder.avg_ns(), recorder.median_ns(),
                recorder.p99_ns(), recorder.max_ns());
}

/// Runs the throughput workload manually (same config as BM_SustainedThroughput)
/// and returns measured orders/sec.
double measure_throughput() {
    using namespace miniexchange;

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
    auto events = gen.generate(kThroughputEvents);

    // Run multiple repetitions and take the best (least noisy) measurement.
    double best_ops_per_sec = 0.0;

    for (std::size_t rep = 0; rep < kThroughputRepetitions; ++rep) {
        MatchingEngine engine;

        auto start = std::chrono::steady_clock::now();
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
        auto end = std::chrono::steady_clock::now();

        double elapsed_sec =
            std::chrono::duration<double>(end - start).count();
        double ops_per_sec =
            static_cast<double>(kThroughputEvents) / elapsed_sec;

        if (ops_per_sec > best_ops_per_sec) {
            best_ops_per_sec = ops_per_sec;
        }
    }

    return best_ops_per_sec;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace miniexchange::benchmark;

    // ─── Latency benchmarks (R1–R3) ────────────────────────────────────
    std::printf("=== Single-operation latency (%zu iterations) ===\n",
                kLatencyIterations);

    LatencyRecorder rec_add_no_match;
    bench_add_no_match(rec_add_no_match, kLatencyIterations);
    print_recorder("ADD (no match)", rec_add_no_match);

    LatencyRecorder rec_add_1fill;
    bench_add_with_match(rec_add_1fill, kLatencyIterations, 1);
    print_recorder("ADD (1 fill)", rec_add_1fill);

    LatencyRecorder rec_add_10fills;
    bench_add_with_match(rec_add_10fills, kLatencyIterations, 10);
    print_recorder("ADD (10 fills)", rec_add_10fills);

    LatencyRecorder rec_add_100fills;
    bench_add_with_match(rec_add_100fills, kLatencyIterations, 100);
    print_recorder("ADD (100 fills)", rec_add_100fills);

    LatencyRecorder rec_cancel_front;
    bench_cancel(rec_cancel_front, kLatencyIterations, true);
    print_recorder("CANCEL (front)", rec_cancel_front);

    LatencyRecorder rec_cancel_back;
    bench_cancel(rec_cancel_back, kLatencyIterations, false);
    print_recorder("CANCEL (back)", rec_cancel_back);

    std::printf("\n");

    // ─── Manual throughput measurement (for results file) ──────────────
    std::printf("=== Sustained throughput (%zu events x %zu reps) ===\n",
                kThroughputEvents, kThroughputRepetitions);
    double throughput = measure_throughput();
    std::printf("  Mixed workload: %.2fM orders/sec (best of %zu reps)\n\n",
                throughput / 1'000'000.0, kThroughputRepetitions);

    // ─── Write results to markdown ─────────────────────────────────────
    std::vector<LatencyResult> latency_results{
        {"ADD (no match)", rec_add_no_match.avg_ns(),
         rec_add_no_match.median_ns(), rec_add_no_match.p99_ns(),
         rec_add_no_match.max_ns()},
        {"ADD (1 fill)", rec_add_1fill.avg_ns(), rec_add_1fill.median_ns(),
         rec_add_1fill.p99_ns(), rec_add_1fill.max_ns()},
        {"ADD (10 fills)", rec_add_10fills.avg_ns(),
         rec_add_10fills.median_ns(), rec_add_10fills.p99_ns(),
         rec_add_10fills.max_ns()},
        {"ADD (100 fills)", rec_add_100fills.avg_ns(),
         rec_add_100fills.median_ns(), rec_add_100fills.p99_ns(),
         rec_add_100fills.max_ns()},
        {"CANCEL (front)", rec_cancel_front.avg_ns(),
         rec_cancel_front.median_ns(), rec_cancel_front.p99_ns(),
         rec_cancel_front.max_ns()},
        {"CANCEL (back)", rec_cancel_back.avg_ns(),
         rec_cancel_back.median_ns(), rec_cancel_back.p99_ns(),
         rec_cancel_back.max_ns()},
    };

    std::vector<ThroughputResult> throughput_results{
        {"Mixed (60% limit, 10% market, 30% cancel)", throughput},
    };

    // Phase 2 baseline values (from benchmarks/results/phase-02-baseline.md)
    // for side-by-side comparison in the Phase 3 results file.
    std::vector<BaselineEntry> phase2_baseline{
        {"ADD (no match)", 900.0},
        {"ADD (1 fill)", 600.0},
        {"ADD (10 fills)", 2300.0},
        {"ADD (100 fills)", 19500.0},
        {"CANCEL (front)", 300.0},
        {"CANCEL (back)", 200.0},
    };
    constexpr double kPhase2Throughput = 2'920'000.0;  // 2.92M orders/sec

    const char* results_path = "benchmarks/results/phase-03-pooled.md";
    write_results(results_path, latency_results, throughput_results,
                  "Windows laptop, no CPU pinning, no turbo-boost control",
                  "Phase 3 — Memory Pool Results",
                  phase2_baseline, kPhase2Throughput);
    std::printf("Results written to: %s\n\n", results_path);

    // ─── Google Benchmark throughput (R4, Task 7) ──────────────────────
    // Also run Google Benchmark's registered BM_SustainedThroughput for
    // standard Google Benchmark output (useful for comparison tooling).
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return EXIT_FAILURE;
    }
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();

    return EXIT_SUCCESS;
}
