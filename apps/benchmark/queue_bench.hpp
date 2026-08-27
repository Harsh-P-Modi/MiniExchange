#ifndef MINIEXCHANGE_APPS_BENCHMARK_QUEUE_BENCH_HPP
#define MINIEXCHANGE_APPS_BENCHMARK_QUEUE_BENCH_HPP

namespace miniexchange {

// Runs the Phase 4 queue comparison benchmark (SpscRingBuffer vs MutexQueue).
// Writes results to benchmarks/results/phase-04-queue-comparison.md.
void run_queue_benchmark();

}  // namespace miniexchange

#endif  // MINIEXCHANGE_APPS_BENCHMARK_QUEUE_BENCH_HPP
