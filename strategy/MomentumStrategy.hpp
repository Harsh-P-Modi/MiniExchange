#ifndef MINIEXCHANGE_STRATEGY_MOMENTUM_STRATEGY_HPP
#define MINIEXCHANGE_STRATEGY_MOMENTUM_STRATEGY_HPP

#include <cstddef>
#include <deque>

#include "core/Types.hpp"
#include "strategy/Strategy.hpp"

namespace miniexchange {

struct MomentumConfig {
    std::size_t lookback_n;   // how many recent trade prices feed the signal
    Quantity order_size;      // size of each directional order
    Price signal_threshold;   // minimum |price delta| over the window to act

    // Bootstrap probe (design.md §8 open item — RESOLVED): on an empty
    // tape, a purely trade-reactive strategy would never act (no trade →
    // no signal → no order → no trade). To break that chicken-and-egg and
    // let the runner generate organic flow, the momentum strategy submits
    // a small "probe" market order every `probe_every_ticks` on_tick calls
    // (alternating side), crossing whatever a maker is resting. Those
    // probe fills seed the tape the momentum signal then reacts to. Set to
    // 0 to disable (the unit-test default: purely trade-reactive).
    std::size_t probe_every_ticks = 0;
    Quantity probe_size = Quantity{1};
};

// MomentumStrategy (design.md §4) — a reactive directional participant.
// It watches the trade tape (on_trade), keeps the last N trade prices,
// and when the price has moved more than a threshold across that window,
// fires a MarketOrder in the direction of the move (buy on up-moves, sell
// on down-moves).
//
// The signal is intentionally naive (R3: sophistication is not the goal).
// It's fire-and-forget: a market order per signal, no resting orders, no
// re-quote logic — a much simpler state machine than the market maker.
// Its purpose is to inject reactive, trend-following order bursts so the
// engine sees more than uniform-random flow.
class MomentumStrategy : public Strategy {
public:
    MomentumStrategy(EngineAPI& engine, ClientId id, MomentumConfig config);

    void on_trade(const Trade& trade) override;
    void on_tick() override;  // periodic bootstrap probe (if configured)

    // Exposed for testing the signal logic without submitting.
    [[nodiscard]] std::size_t window_size() const { return prices_.size(); }

private:
    MomentumConfig config_;
    std::deque<Price> prices_;  // most-recent-last, capped at lookback_n
    OrderId next_order_id_;
    std::size_t tick_count_ = 0;
    bool probe_buy_ = true;  // alternates probe side

    OrderId fresh_id() {
        OrderId id = next_order_id_;
        next_order_id_ = OrderId{next_order_id_.value + 1};
        return id;
    }
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_STRATEGY_MOMENTUM_STRATEGY_HPP
