#ifndef MINIEXCHANGE_TOOLS_WORKLOAD_GENERATOR_HPP
#define MINIEXCHANGE_TOOLS_WORKLOAD_GENERATOR_HPP

#include <cstdint>
#include <random>
#include <variant>
#include <vector>

#include "core/NewOrder.hpp"
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

// CancelRequest — a workload-generation-only type representing a cancel
// action. Not part of core/ because the engine's EngineAPI::cancel()
// takes a bare OrderId; this struct gives WorkloadEvent's variant a
// distinct alternative for std::visit dispatch.
struct CancelRequest {
    OrderId id;
};

// WorkloadEvent — one synthetic event the generator can produce.
// Separate from NewOrder because CANCEL needs to reference a previously
// generated, still-resting OrderId.
using WorkloadEvent = std::variant<LimitOrder, MarketOrder, CancelRequest>;

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
