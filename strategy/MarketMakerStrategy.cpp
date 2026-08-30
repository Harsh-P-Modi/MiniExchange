#include "strategy/MarketMakerStrategy.hpp"

#include "core/NewOrder.hpp"

namespace miniexchange {

MarketMakerStrategy::MarketMakerStrategy(EngineAPI& engine, ClientId id,
                                         MarketMakerConfig config)
    : Strategy(engine, id),
      config_(config),
      // Start strategy-local order IDs high and namespaced by ClientId so
      // multiple concurrent strategies don't collide on OrderId (the
      // engine enforces lifetime-unique IDs across ALL clients).
      next_order_id_(OrderId{id.value * 1'000'000'000ULL + 1}),
      current_ref_(config.reference_price) {}

void MarketMakerStrategy::on_tick() {
    if (!quoting_) {
        // First tick places the opening quote.
        quoting_ = true;
        requote();
        return;
    }
    // With a configured drift, the reference oscillates and the maker
    // re-quotes at the new level each tick. The drift oscillates within
    // +/- 4*spread of the base so quotes cross previous quotes (generating
    // trades) without wandering off to infinity. With drift 0 this is a
    // no-op and the maker just sits on its opening quote.
    if (config_.drift_per_tick != 0) {
        const int64_t bound = config_.spread.value * 4;
        drift_accum_ += drift_dir_ * config_.drift_per_tick;
        if (drift_accum_ > bound) {
            drift_accum_ = bound;
            drift_dir_ = -1;
        } else if (drift_accum_ < -bound) {
            drift_accum_ = -bound;
            drift_dir_ = 1;
        }
        current_ref_ = Price{config_.reference_price.value + drift_accum_};
        requote();
    }
}

void MarketMakerStrategy::on_trade(const Trade& trade) {
    // Did this trade hit one of our resting quotes? Our bid sits on the
    // buy side, our ask on the sell side.
    const bool our_bid_hit =
        (bid_id_.value != 0) && (trade.buy_order_id == bid_id_);
    const bool our_ask_hit =
        (ask_id_.value != 0) && (trade.sell_order_id == ask_id_);

    if (!our_bid_hit && !our_ask_hit) {
        return;  // someone else's trade — nothing to do
    }

    // If a resting quote was fully consumed by this trade, it's no longer
    // in the book, so we must not try to cancel it in requote().
    if (our_bid_hit && trade.resting_order_removed) {
        bid_id_ = OrderId{0};
    }
    if (our_ask_hit && trade.resting_order_removed) {
        ask_id_ = OrderId{0};
    }

    // Re-quote both sides (design.md §8 resolution).
    requote();
}

void MarketMakerStrategy::requote() {
    // Cancel-then-new (Q2): pull any still-resting quotes first.
    if (bid_id_.value != 0) {
        engine_.cancel(bid_id_);
        bid_id_ = OrderId{0};
    }
    if (ask_id_.value != 0) {
        engine_.cancel(ask_id_);
        ask_id_ = OrderId{0};
    }

    // Post a fresh bid below and ask above the (possibly drifted)
    // reference price.
    const Price bid_px{current_ref_.value - config_.spread.value};
    const Price ask_px{current_ref_.value + config_.spread.value};

    OrderId new_bid = fresh_id();
    LimitOrder bid{new_bid, Side::Buy, bid_px, config_.quote_size};
    bid.owner = id_;
    EngineResponse bid_resp = engine_.submit(NewOrder{bid});
    // Only track it as resting if it was accepted and didn't immediately
    // fully fill (remaining > 0 means it's on the book).
    if (bid_resp.status == EngineResult::Accepted &&
        bid_resp.remaining_qty.value > 0) {
        bid_id_ = new_bid;
    }

    OrderId new_ask = fresh_id();
    LimitOrder ask{new_ask, Side::Sell, ask_px, config_.quote_size};
    ask.owner = id_;
    EngineResponse ask_resp = engine_.submit(NewOrder{ask});
    if (ask_resp.status == EngineResult::Accepted &&
        ask_resp.remaining_qty.value > 0) {
        ask_id_ = new_ask;
    }
}

}  // namespace miniexchange
