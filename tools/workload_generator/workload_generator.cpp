#include "tools/workload_generator/workload_generator.hpp"

#include <algorithm>
#include <cmath>

namespace miniexchange {

WorkloadGenerator::WorkloadGenerator(WorkloadConfig config)
    : config_(config),
      rng_(config.seed),
      price_offset_dist_(0.0, config.price_stddev_log),
      quantity_dist_(config.quantity_min.value, config.quantity_max.value),
      assumed_resting_(),
      next_id_(1) {}

std::vector<WorkloadEvent> WorkloadGenerator::generate(size_t count) {
    std::vector<WorkloadEvent> events;
    events.reserve(count);

    // Cumulative thresholds for event type selection
    const double limit_threshold = config_.add_limit_ratio;
    const double market_threshold = limit_threshold + config_.add_market_ratio;
    // cancel_threshold is implicitly 1.0

    std::uniform_real_distribution<double> type_dist(0.0, 1.0);
    std::uniform_int_distribution<int> side_dist(0, 1);

    for (size_t i = 0; i < count; ++i) {
        double roll = type_dist(rng_);

        if (roll < limit_threshold) {
            // Generate a LimitOrder
            Side side = (side_dist(rng_) == 0) ? Side::Buy : Side::Sell;

            // Price: symmetric log-normal offset from mid_price
            double offset_raw = price_offset_dist_(rng_);
            int64_t offset = static_cast<int64_t>(std::round(offset_raw));
            if (offset < 1) offset = 1;  // ensure non-zero offset

            // Randomly negate for symmetric spread
            int64_t price_val;
            if (side_dist(rng_) == 0) {
                price_val = config_.mid_price.value + offset;
            } else {
                price_val = config_.mid_price.value - offset;
            }
            // Clamp to positive price
            if (price_val <= 0) price_val = 1;

            Quantity qty{quantity_dist_(rng_)};
            OrderId id{next_id_++};

            events.emplace_back(LimitOrder{id, side, Price{price_val}, qty});

            // Track as resting (limit orders can rest on the book)
            assumed_resting_.push_back(id);

        } else if (roll < market_threshold) {
            // Generate a MarketOrder
            Side side = (side_dist(rng_) == 0) ? Side::Buy : Side::Sell;
            Quantity qty{quantity_dist_(rng_)};
            OrderId id{next_id_++};

            events.emplace_back(MarketOrder{id, side, qty});
            // Market orders never rest — do NOT add to assumed_resting_

        } else {
            // Generate a CancelRequest
            if (assumed_resting_.empty()) {
                // Fallback: no resting orders to cancel, generate a
                // LimitOrder instead to avoid producing invalid cancels
                Side side = (side_dist(rng_) == 0) ? Side::Buy : Side::Sell;

                double offset_raw = price_offset_dist_(rng_);
                int64_t offset = static_cast<int64_t>(std::round(offset_raw));
                if (offset < 1) offset = 1;

                int64_t price_val;
                if (side_dist(rng_) == 0) {
                    price_val = config_.mid_price.value + offset;
                } else {
                    price_val = config_.mid_price.value - offset;
                }
                if (price_val <= 0) price_val = 1;

                Quantity qty{quantity_dist_(rng_)};
                OrderId id{next_id_++};

                events.emplace_back(
                    LimitOrder{id, side, Price{price_val}, qty});
                assumed_resting_.push_back(id);
            } else {
                // Pick a random resting order to cancel
                std::uniform_int_distribution<size_t> idx_dist(
                    0, assumed_resting_.size() - 1);
                size_t idx = idx_dist(rng_);

                OrderId cancel_id = assumed_resting_[idx];

                // Swap-and-pop removal (O(1), order doesn't matter)
                assumed_resting_[idx] = assumed_resting_.back();
                assumed_resting_.pop_back();

                events.emplace_back(CancelRequest{cancel_id});
            }
        }
    }

    return events;
}

}  // namespace miniexchange
