#!/bin/bash
# Phase 2 — Benchmark runner with optional CPU pinning.
#
# Usage:
#   ./scripts/run_benchmarks.sh              # runs with taskset on core 0
#   ./scripts/run_benchmarks.sh --no-pin     # runs without CPU pinning
#   CPU_CORE=3 ./scripts/run_benchmarks.sh   # pin to core 3
#
# To disable turbo boost / frequency scaling for more stable measurements:
#   echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
#   # OR for AMD:
#   echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost
#   # To set performance governor (locks frequency):
#   sudo cpupower frequency-set -g performance
#
# These vary by machine/BIOS. Document which were applied in the results file.
# The benchmark itself does NOT require any of this — it's a recommendation
# for more reproducible measurements.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCHMARK_BIN="$PROJECT_ROOT/build/benchmark_harness"

# Default: pin to core 0
CPU_CORE="${CPU_CORE:-0}"

if [[ ! -f "$BENCHMARK_BIN" ]]; then
    echo "ERROR: benchmark_harness not found at $BENCHMARK_BIN"
    echo "Run: cmake --build build --target benchmark_harness"
    exit 1
fi

if [[ "${1:-}" == "--no-pin" ]]; then
    echo "Running benchmark WITHOUT CPU pinning..."
    "$BENCHMARK_BIN"
else
    echo "Running benchmark pinned to core $CPU_CORE (use --no-pin to disable)..."
    taskset -c "$CPU_CORE" "$BENCHMARK_BIN"
fi
