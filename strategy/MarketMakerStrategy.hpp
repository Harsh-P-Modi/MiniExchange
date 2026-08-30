#ifndef MINIEXCHANGE_STRATEGY_MARKET_MAKER_STRATEGY_HPP
#define MINIEXCHANGE_STRATEGY_MARKET_MAKER_STRATEGY_HPP

#include "core/Types.hpp"
#include "strategy/Strategy.hpp"

namespace miniexchange {

struct MarketMakerConfig {
    Price reference_price;  // mid the quotes centre on
    Price spread;           // half-spread: bid = ref - spread, ask = ref + spread
    Quantity quote_size;    // size of each quote

    // Per-tick reference drift (design.md §8 open item — RESOLVED):
    // the reference oscillates by this many ticks each on_tick, so the
    // maker periodically re-quotes at new levels. When the drift carries
    // a new quote across the opposite resting quote, a trade occurs —
    // which is what bootstraps organic order flow (and feeds the momentum
    // strategy). 0 = fully static (the unit-test default: quote once, sit
    // still). The runner uses a non-zero drift so the session actually
    // generates trades (R5).
    int64_t drift_per_tick = 0;
};

// MarketMakerStrategy (design.md §3) — a passive liquidity provider. It
// posts a two-sided quote (a bid below and an ask above a reference
// price) and, whenever either side is hit, cancels whatever quote(s)
// remain and re-posts a fresh two-sided quote.
//
// Not profit-seeking (Charter non-goal): its job is to generate
// continuous, realistic two-sided resting-order traffic to exercise the
// engine. A competitor could pick it off through the gap between the
// cancel and the re-quote — deliberately acceptable.
//
// On-fill behavior (design.md §8 open item — RESOLVED here): re-quote
// BOTH sides on any fill (cancel-and-replace both), not just the filled
// side. Rationale: it keeps a live two-sided quote at all times and
// produces steadier, more realistic order flow than replacing only one
// side, which is this phase's actual goal.
//
// Re-quote uses cancel-then-new (two EngineAPI calls, per Q2) — no
// Cancel-Replace order type is introduced (that stayed out of scope in
// Phase 1).
class MarketMakerStrategy : public Strategy {
public:
    MarketMakerStrategy(EngineAPI& engine, ClientId id, MarketMakerConfig config);

    void on_tick() override;                 // places the initial quote
    void on_trade(const Trade& trade) override;  // detects our quotes being hit

private:
    MarketMakerConfig config_;
    OrderId next_order_id_;   // strategy-local monotonic id source
    OrderId bid_id_{0};       // currently-resting bid (0 = none)
    OrderId ask_id_{0};       // currently-resting ask (0 = none)
    bool quoting_ = false;    // whether we've placed our first quote
    Price current_ref_;       // reference, drifts each tick if configured
    int64_t drift_dir_ = 1;   // +1/-1, flips at drift bounds for oscillation
    int64_t drift_accum_ = 0;  // net drift from the base reference

    // Cancel any resting quotes, then post a fresh bid+ask around the
    // reference price. This is the cancel-then-new re-quote (Q2).
    void requote();

    OrderId fresh_id() {
        OrderId id = next_order_id_;
        next_order_id_ = OrderId{next_order_id_.value + 1};
        return id;
    }
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_STRATEGY_MARKET_MAKER_STRATEGY_HPP
