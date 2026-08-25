#ifndef MINIEXCHANGE_APPS_BENCHMARK_RESULTS_WRITER_HPP
#define MINIEXCHANGE_APPS_BENCHMARK_RESULTS_WRITER_HPP

#include <string>
#include <vector>

namespace miniexchange::benchmark {

/// A single latency benchmark result row (one operation type).
struct LatencyResult {
    std::string label;
    double avg_ns;
    double median_ns;
    double p99_ns;
    double max_ns;
};

/// A single throughput benchmark result row.
struct ThroughputResult {
    std::string label;
    double orders_per_sec;
};

/// Writes benchmark results to a markdown file per design.md §7 format.
/// Creates parent directories if they don't exist.
void write_results(const std::string& filepath,
                   const std::vector<LatencyResult>& latency_results,
                   const std::vector<ThroughputResult>& throughput_results,
                   const std::string& environment_description);

}  // namespace miniexchange::benchmark

#endif  // MINIEXCHANGE_APPS_BENCHMARK_RESULTS_WRITER_HPP
