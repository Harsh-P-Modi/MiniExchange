#ifndef MINIEXCHANGE_APPS_BENCHMARK_LATENCY_RECORDER_HPP
#define MINIEXCHANGE_APPS_BENCHMARK_LATENCY_RECORDER_HPP

#include <chrono>
#include <cstddef>
#include <vector>

namespace miniexchange::benchmark {

/// Records per-operation latency samples and computes summary statistics.
/// Sorting happens on read (once, after measurement), never in a hot path.
class LatencyRecorder {
  public:
    /// Append a single duration sample.
    void record(std::chrono::nanoseconds duration);

    /// Arithmetic mean of all recorded samples, in nanoseconds.
    /// Returns 0.0 if no samples have been recorded.
    double avg_ns() const;

    /// Median (50th percentile) of recorded samples, in nanoseconds.
    /// For even count, returns the average of the two middle elements.
    /// Returns 0.0 if no samples have been recorded.
    double median_ns() const;

    /// 99th percentile of recorded samples, in nanoseconds.
    /// Uses index = ceil(0.99 * count) - 1 after sorting.
    /// Returns 0.0 if no samples have been recorded.
    double p99_ns() const;

    /// Maximum recorded sample, in nanoseconds.
    /// Returns 0.0 if no samples have been recorded.
    double max_ns() const;

    /// Number of samples recorded so far.
    std::size_t count() const;

  private:
    std::vector<std::chrono::nanoseconds> samples_;
};

} // namespace miniexchange::benchmark

#endif // MINIEXCHANGE_APPS_BENCHMARK_LATENCY_RECORDER_HPP
