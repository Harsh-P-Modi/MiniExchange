#ifndef MINIEXCHANGE_TOOLS_WORKLOAD_GENERATOR_HPP
#define MINIEXCHANGE_TOOLS_WORKLOAD_GENERATOR_HPP

#include <cstdint>
#include <random>
#include <vector>

#include "core/EngineCommand.hpp"
#include "core/Types.hpp"

namespace miniexchange {

// WorkloadConfig — all parameters that control the workload generator's
// output. Every field is deterministic given the seed; no other source
// of randomness exists in WorkloadGenerator.
struct WorkloadConfig {
    uint64_t seed;
    Price mid_price;
    double price_stddev_log;      // log-normal sigma for |offset| from mid
    Quantity quantity_min;
    Quantity quantity_max;         // uniform within [min, max]
    double add_limit_ratio;       // mix ratios, must sum to 1.0
    double add_market_ratio;
    double cancel_ratio;
};

// WorkloadEvent — one synthetic event the generator can produce.
// Phase 4 tidy-up: aliased to EngineCommand (the canonical engine-facing
// command variant from core/EngineCommand.hpp), which carries the same
// three alternatives: LimitOrder, MarketOrder, CancelRequest. The local
// CancelRequest definition that previously lived here has been removed —
// CancelRequest is now the canonical one from core/EngineCommand.hpp.
using WorkloadEvent = EngineCommand;

// WorkloadGenerator — produces deterministic sequences of synthetic
// exchange events for benchmarking and (later) strategy testing.
// Seeded exclusively from WorkloadConfig::seed (R5).
class WorkloadGenerator {
public:
    explicit WorkloadGenerator(WorkloadConfig config);

    // Generates `count` events. Internally tracks which OrderIds it has
    // generated as LimitOrders and not yet cancelled, so CANCEL events
    // reference plausible still-resting orders. Market orders are never
    // tracked (they never rest per Phase 1's R10).
    std::vector<WorkloadEvent> generate(size_t count);

private:
    WorkloadConfig config_;
    std::mt19937_64 rng_;
    std::lognormal_distribution<double> price_offset_dist_;
    std::uniform_int_distribution<uint64_t> quantity_dist_;
    std::vector<OrderId> assumed_resting_;  // candidates for CANCEL
    uint64_t next_id_ = 1;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_TOOLS_WORKLOAD_GENERATOR_HPP
