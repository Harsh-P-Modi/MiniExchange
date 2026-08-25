#include "apps/benchmark/latency_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace miniexchange::benchmark {

void LatencyRecorder::record(std::chrono::nanoseconds duration) {
    samples_.push_back(duration);
}

double LatencyRecorder::avg_ns() const {
    if (samples_.empty()) {
        return 0.0;
    }
    auto sum = std::accumulate(
        samples_.begin(), samples_.end(), std::chrono::nanoseconds{0});
    return static_cast<double>(sum.count()) /
           static_cast<double>(samples_.size());
}

double LatencyRecorder::median_ns() const {
    if (samples_.empty()) {
        return 0.0;
    }

    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    auto n = sorted.size();
    if (n % 2 == 1) {
        return static_cast<double>(sorted[n / 2].count());
    }
    // Even count: average the two middle elements.
    return (static_cast<double>(sorted[n / 2 - 1].count()) +
            static_cast<double>(sorted[n / 2].count())) /
           2.0;
}

double LatencyRecorder::p99_ns() const {
    if (samples_.empty()) {
        return 0.0;
    }

    auto sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    // Index = ceil(0.99 * count) - 1
    auto n = sorted.size();
    auto index =
        static_cast<std::size_t>(std::ceil(0.99 * static_cast<double>(n))) - 1;
    return static_cast<double>(sorted[index].count());
}

double LatencyRecorder::max_ns() const {
    if (samples_.empty()) {
        return 0.0;
    }
    auto it = std::max_element(samples_.begin(), samples_.end());
    return static_cast<double>(it->count());
}

std::size_t LatencyRecorder::count() const {
    return samples_.size();
}

} // namespace miniexchange::benchmark
