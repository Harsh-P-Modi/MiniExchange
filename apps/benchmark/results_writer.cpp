#include "apps/benchmark/results_writer.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace miniexchange::benchmark {

void write_results(const std::string& filepath,
                   const std::vector<LatencyResult>& latency_results,
                   const std::vector<ThroughputResult>& throughput_results,
                   const std::string& environment_description) {
    // Create parent directories if they don't exist.
    std::filesystem::path path(filepath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(filepath);
    if (!out.is_open()) {
        std::fprintf(stderr, "ERROR: could not open %s for writing\n",
                     filepath.c_str());
        return;
    }

    out << "# Phase 2 Baseline Results\n\n";
    out << "Environment: " << environment_description << "\n";
    out << "Build: RelWithDebInfo\n\n";

    // --- Single-operation latency table ---
    out << "## Single-operation latency (ns)\n\n";
    out << "| Operation | Avg | Median | P99 | Max |\n";
    out << "|---|---|---|---|---|\n";

    // Format a double as an integer if it's a whole number, otherwise 1 decimal.
    auto fmt = [](double val) -> std::string {
        char buf[64];
        if (val == static_cast<double>(static_cast<int64_t>(val))) {
            std::snprintf(buf, sizeof(buf), "%.0f", val);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f", val);
        }
        return buf;
    };

    for (const auto& r : latency_results) {
        out << "| " << r.label << " | " << fmt(r.avg_ns) << " | "
            << fmt(r.median_ns) << " | " << fmt(r.p99_ns) << " | "
            << fmt(r.max_ns) << " |\n";
    }

    out << "\n";

    // --- Sustained throughput table ---
    out << "## Sustained throughput\n\n";
    out << "| Workload | Orders/sec |\n";
    out << "|---|---|\n";

    for (const auto& r : throughput_results) {
        char buf[64];
        if (r.orders_per_sec >= 1'000'000.0) {
            std::snprintf(buf, sizeof(buf), "%.2fM", r.orders_per_sec / 1'000'000.0);
        } else if (r.orders_per_sec >= 1'000.0) {
            std::snprintf(buf, sizeof(buf), "%.0fK", r.orders_per_sec / 1'000.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%.0f", r.orders_per_sec);
        }
        out << "| " << r.label << " | " << buf << " |\n";
    }
}

}  // namespace miniexchange::benchmark
