#include "apps/benchmark/latency_recorder.hpp"
#include "apps/benchmark/mutex_queue.hpp"
#include "core/EngineCommand.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"
#include "tools/workload_generator/workload_generator.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace miniexchange {
namespace {

// --- Configuration ---
static constexpr std::size_t kQueueCapacity = 4096;
static constexpr std::size_t kLatencyOps = 100'000;
static constexpr std::size_t kThroughputOps = 2'000'000;
static constexpr int kThroughputReps = 5;

// Generate a workload of EngineCommands
std::vector<EngineCommand> generate_workload(std::size_t count) {
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
    return gen.generate(count);
}

// --- Mode (a): Isolated per-operation latency (single-threaded) ---
// Measures the cost of try_push + try_pop individually, no concurrency.

struct LatencyResult {
    double avg_ns;
    double median_ns;
    double p99_ns;
    double max_ns;
};

template <typename Queue>
LatencyResult measure_push_latency(const std::vector<EngineCommand>& workload) {
    benchmark::LatencyRecorder recorder;
    Queue queue;

    for (std::size_t i = 0; i < workload.size(); ++i) {
        auto start = std::chrono::steady_clock::now();
        queue.try_push(workload[i]);
        auto end = std::chrono::steady_clock::now();
        recorder.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));

        // Keep queue from filling: pop every other push
        if (i % 2 == 1) {
            EngineCommand tmp;
            queue.try_pop(tmp);
        }
    }
    return {recorder.avg_ns(), recorder.median_ns(), recorder.p99_ns(),
            recorder.max_ns()};
}

template <typename Queue>
LatencyResult measure_pop_latency(const std::vector<EngineCommand>& workload) {
    benchmark::LatencyRecorder recorder;
    Queue queue;

    // Pre-fill some items, then measure pop latency
    for (std::size_t i = 0; i < workload.size(); ++i) {
        queue.try_push(workload[i]);

        // Pop and measure
        EngineCommand tmp;
        auto start = std::chrono::steady_clock::now();
        queue.try_pop(tmp);
        auto end = std::chrono::steady_clock::now();
        recorder.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
    return {recorder.avg_ns(), recorder.median_ns(), recorder.p99_ns(),
            recorder.max_ns()};
}

// --- Mode (b): Real two-thread producer/consumer throughput ---

template <typename Queue>
double measure_throughput(const std::vector<EngineCommand>& workload) {
    Queue queue;
    std::atomic<bool> producer_done{false};

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (const auto& cmd : workload) {
            while (!queue.try_push(cmd)) {
                // spin on full
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        EngineCommand tmp;
        while (true) {
            if (queue.try_pop(tmp)) {
                // consumed
            } else if (producer_done.load(std::memory_order_acquire)) {
                while (queue.try_pop(tmp)) {}
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          end - start)
                          .count();
    return static_cast<double>(workload.size()) /
           (static_cast<double>(elapsed_ns) / 1e9);
}

// Run throughput N times, take best
template <typename Queue>
double best_throughput(const std::vector<EngineCommand>& workload, int reps) {
    double best = 0.0;
    for (int i = 0; i < reps; ++i) {
        double t = measure_throughput<Queue>(workload);
        if (t > best) best = t;
    }
    return best;
}

void write_results(const std::string& path,
                   const LatencyResult& spsc_push,
                   const LatencyResult& spsc_pop,
                   const LatencyResult& mutex_push,
                   const LatencyResult& mutex_pop,
                   double spsc_throughput,
                   double mutex_throughput) {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream out(path);

    out << "# Phase 4 — Queue Comparison: Lock-Free vs. Mutex Baseline\n\n";
    out << "**Environment:** Windows laptop, no CPU pinning, "
           "RelWithDebInfo build, "
        << kQueueCapacity << "-slot queue capacity\n\n";

    out << "## Isolated Per-Operation Latency (single-threaded)\n\n";
    out << "| Operation | Queue | Avg (ns) | Median (ns) | P99 (ns) "
           "| Max (ns) |\n";
    out << "|---|---|---|---|---|---|\n";

    auto row = [&](const std::string& op, const std::string& qtype,
                   const LatencyResult& r) {
        out << "| " << op << " | " << qtype << " | "
            << std::fixed << std::setprecision(1)
            << r.avg_ns << " | " << r.median_ns << " | " << r.p99_ns
            << " | " << r.max_ns << " |\n";
    };

    row("try_push", "SpscRingBuffer", spsc_push);
    row("try_push", "MutexQueue", mutex_push);
    row("try_pop", "SpscRingBuffer", spsc_pop);
    row("try_pop", "MutexQueue", mutex_pop);

    out << "\n## Two-Thread Producer/Consumer Throughput\n\n";
    out << "| Queue | Throughput (ops/sec) |\n";
    out << "|---|---|\n";
    out << "| SpscRingBuffer | " << std::fixed << std::setprecision(0)
        << spsc_throughput << " |\n";
    out << "| MutexQueue | " << std::fixed << std::setprecision(0)
        << mutex_throughput << " |\n";

    double speedup = spsc_throughput / mutex_throughput;
    out << "\n**Speedup:** SpscRingBuffer is " << std::fixed
        << std::setprecision(2) << speedup << "x faster than MutexQueue "
        << "in two-thread throughput.\n";

    out << "\n## Interpretation\n\n";
    out << "- **Isolated latency (single-threaded):** With no contention, "
           "the mutex has minimal overhead (no actual blocking occurs). "
           "The lock-free buffer may show similar or slightly better numbers "
           "due to avoiding the mutex syscall overhead entirely — even an "
           "uncontended mutex requires an atomic compare-and-swap on Linux "
           "(futex fast path) or a kernel transition on Windows.\n";
    out << "- **Two-thread throughput:** This is where the lock-free buffer "
           "should clearly outperform. Under sustained producer/consumer "
           "load, the mutex forces serialization (one thread waits while the "
           "other holds the lock), while the ring buffer allows both threads "
           "to progress simultaneously — the producer writes to tail without "
           "observing head's cache line (until checking if full), and the "
           "consumer reads from head without touching tail's cache line "
           "(until checking if empty).\n";
    out << "- **Tail latency (P99/max):** The mutex's worst-case is "
           "unbounded under contention (thread can be descheduled while "
           "holding the lock, blocking the other indefinitely). The ring "
           "buffer's worst case is bounded by the try_push/try_pop "
           "operation itself (a few cache misses at most). This difference "
           "matters most under load — exactly Phase 5's scenario.\n";

    out.close();
}

}  // namespace

void run_queue_benchmark() {
    std::cout << "Phase 4 Queue Benchmark\n";
    std::cout << "=======================\n\n";

    // Generate workloads
    std::cout << "Generating workload (" << kLatencyOps
              << " ops for latency)...\n";
    auto latency_workload = generate_workload(kLatencyOps);

    std::cout << "Generating workload (" << kThroughputOps
              << " ops for throughput)...\n";
    auto throughput_workload = generate_workload(kThroughputOps);

    // Mode (a): Isolated latency
    using Spsc = SpscRingBuffer<EngineCommand, kQueueCapacity>;
    using Mutex = MutexQueue<EngineCommand, kQueueCapacity>;

    std::cout << "Measuring SpscRingBuffer push latency...\n";
    auto spsc_push = measure_push_latency<Spsc>(latency_workload);
    std::cout << "Measuring SpscRingBuffer pop latency...\n";
    auto spsc_pop = measure_pop_latency<Spsc>(latency_workload);
    std::cout << "Measuring MutexQueue push latency...\n";
    auto mutex_push = measure_push_latency<Mutex>(latency_workload);
    std::cout << "Measuring MutexQueue pop latency...\n";
    auto mutex_pop = measure_pop_latency<Mutex>(latency_workload);

    // Mode (b): Two-thread throughput
    std::cout << "Measuring SpscRingBuffer throughput (" << kThroughputReps
              << " reps, best-of)...\n";
    double spsc_tp = best_throughput<Spsc>(throughput_workload, kThroughputReps);
    std::cout << "Measuring MutexQueue throughput (" << kThroughputReps
              << " reps, best-of)...\n";
    double mutex_tp =
        best_throughput<Mutex>(throughput_workload, kThroughputReps);

    // Print summary
    std::cout << "\n--- Results ---\n";
    std::cout << "SpscRingBuffer push: median=" << spsc_push.median_ns
              << "ns, P99=" << spsc_push.p99_ns << "ns\n";
    std::cout << "MutexQueue push: median=" << mutex_push.median_ns
              << "ns, P99=" << mutex_push.p99_ns << "ns\n";
    std::cout << "SpscRingBuffer pop: median=" << spsc_pop.median_ns
              << "ns, P99=" << spsc_pop.p99_ns << "ns\n";
    std::cout << "MutexQueue pop: median=" << mutex_pop.median_ns
              << "ns, P99=" << mutex_pop.p99_ns << "ns\n";
    std::cout << "SpscRingBuffer throughput: " << spsc_tp << " ops/sec\n";
    std::cout << "MutexQueue throughput: " << mutex_tp << " ops/sec\n";
    std::cout << "Speedup: " << (spsc_tp / mutex_tp) << "x\n";

    // Write results file
    std::string results_path =
        "benchmarks/results/phase-04-queue-comparison.md";
    write_results(results_path, spsc_push, spsc_pop, mutex_push, mutex_pop,
                  spsc_tp, mutex_tp);
    std::cout << "\nResults written to " << results_path << "\n";
}

}  // namespace miniexchange
