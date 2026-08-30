#include "strategy/MomentumStrategy.hpp"

#include "core/NewOrder.hpp"

namespace miniexchange {

MomentumStrategy::MomentumStrategy(EngineAPI& engine, ClientId id,
                                   MomentumConfig config)
    : Strategy(engine, id),
      config_(config),
      next_order_id_(OrderId{id.value * 1'000'000'000ULL + 1}) {}

void MomentumStrategy::on_trade(const Trade& trade) {
    // Append the newest trade price, keeping at most lookback_n samples.
    prices_.push_back(trade.price);
    if (prices_.size() > config_.lookback_n) {
        prices_.pop_front();
    }

    // Need a full window before the signal is meaningful.
    if (prices_.size() < config_.lookback_n || config_.lookback_n == 0) {
        return;
    }

    // Naive signal: newest price minus oldest price in the window.
    const int64_t delta = prices_.back().value - prices_.front().value;

    if (delta > config_.signal_threshold.value) {
        // Upward momentum -> buy market order.
        MarketOrder mo{fresh_id(), Side::Buy, config_.order_size};
        mo.owner = id_;
        EngineResponse resp = engine_.submit(NewOrder{mo});
        on_response(resp);
    } else if (-delta > config_.signal_threshold.value) {
        // Downward momentum -> sell market order.
        MarketOrder mo{fresh_id(), Side::Sell, config_.order_size};
        mo.owner = id_;
        EngineResponse resp = engine_.submit(NewOrder{mo});
        on_response(resp);
    }
    // else: move too small, no action.
}

void MomentumStrategy::on_tick() {
    if (config_.probe_every_ticks == 0) return;  // probing disabled
    if (++tick_count_ % config_.probe_every_ticks != 0) return;

    // Fire a small market order, alternating side, to cross a resting
    // maker quote and seed the tape. Fire-and-forget.
    MarketOrder mo{fresh_id(), probe_buy_ ? Side::Buy : Side::Sell,
                   config_.probe_size};
    mo.owner = id_;
    probe_buy_ = !probe_buy_;
    EngineResponse resp = engine_.submit(NewOrder{mo});
    on_response(resp);
}

}  // namespace miniexchange
