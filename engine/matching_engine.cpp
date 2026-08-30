#include "engine/matching_engine.hpp"

#include <type_traits>
#include <variant>

namespace miniexchange {

MatchingEngine::MatchingEngine(EventSink* sink, std::size_t pool_capacity,
                               StpConfig stp)
    : book_(pool_capacity), sink_(sink), stp_(stp) {}

EngineResponse MatchingEngine::submit(const NewOrder& order) {
    return std::visit(
        [this](const auto& o) -> EngineResponse {
            if constexpr (std::is_same_v<std::decay_t<decltype(o)>,
                                         LimitOrder>) {
                return submit_limit(o);
            } else {
                return submit_market(o);
            }
        },
        order);
}

EngineResponse MatchingEngine::cancel(OrderId id) {
    // Look up the order in the book — cancel only cares about currently
    // resting orders. The ever_seen_ids_ set is NOT consulted here:
    // an ID that was accepted and then fully filled is NOT in the book,
    // so cancel correctly returns UnknownOrderId for it (R13).
    Order* order = book_.find_order(id);
    if (order == nullptr) {
        // R13: unknown, already filled, or already cancelled.
        return EngineResponse{EngineResult::UnknownOrderId, {}, Quantity{0}};
    }

    // Capture fields before removal destroys the Order.
    Quantity remaining = order->quantity;
    Side side = order->side;
    Price price = order->price;

    // R12: remove from book (O(1) via intrusive list + hash map).
    book_.remove_order(order);

    // R18: emit on_order_cancelled exactly once (R20: synchronous).
    sink_->on_order_cancelled(OrderCancelled{id, remaining, side, price});

    return EngineResponse{EngineResult::Accepted, {}, remaining};
}

const OrderBook& MatchingEngine::book() const {
    return book_;
}

EngineResponse MatchingEngine::submit_limit(const LimitOrder& order) {
    // Validation: R3 — zero quantity rejected.
    if (order.quantity == Quantity{0}) {
        return EngineResponse{EngineResult::InvalidQuantity, {}, Quantity{0}};
    }

    // Validation: R4 — non-positive price rejected.
    if (order.price <= Price{0}) {
        return EngineResponse{EngineResult::InvalidPrice, {}, Quantity{0}};
    }

    // Validation: R2 — duplicate OrderId (lifetime-unique per §2.1).
    if (ever_seen_ids_.contains(order.id)) {
        return EngineResponse{EngineResult::DuplicateOrderId, {}, Quantity{0}};
    }

    // Phase 8 R5 — self-trade prevention, RejectIncoming policy.
    // Pre-scan BEFORE any mutation (no ID recorded, no event, no fill):
    // if this order would cross a resting order of the same owner, reject
    // it now. This preserves NFR2 — a rejected order consumes no OrderId.
    // CancelResting is handled inside the match loop instead (below).
    if (stp_.enabled && stp_.policy == StpPolicy::RejectIncoming &&
        would_self_cross(order.side, order.owner, order.price)) {
        return EngineResponse{EngineResult::SelfTradePrevented, {},
                              Quantity{0}};
    }

    // R4 (Phase 3): reject if pool exhausted — before any side effects.
    // Pre-checking guarantees that if matching leaves a remainder, at least
    // one slot is available to rest it. Matching can only release slots
    // (fully filled resting orders return theirs), so availability can only
    // increase during matching — one pre-check is sufficient.
    if (book_.pool_available() == 0) {
        return EngineResponse{EngineResult::PoolExhausted, {}, Quantity{0}};
    }

    // Accept the order: record the ID as ever-seen.
    ever_seen_ids_.insert(order.id);

    // Emit on_order_accepted (R16) — emitted before matching begins.
    sink_->on_order_accepted(
        OrderAccepted{order.id, order.side, order.quantity, order.price});

    // Perform matching against opposite side (R5-R8).
    Quantity remaining = order.quantity;
    std::vector<Trade> trades = match_against_book(
        order.side, order.id, order.owner, remaining, order.price);

    // If there's remaining quantity after matching, rest on the book (R8).
    if (remaining > Quantity{0}) {
        Order order_data{
            .id = order.id,
            .side = order.side,
            .price = order.price,
            .quantity = remaining,
            .sequence = next_sequence_++,
            .owner = order.owner,  // Phase 8: carry submitter identity onto
                                   // the resting order for STP (R5).
            .prev = nullptr,
            .next = nullptr,
            .level = nullptr,
        };
        book_.add_order(order_data);
    }

    return EngineResponse{EngineResult::Accepted, std::move(trades), remaining};
}

EngineResponse MatchingEngine::submit_market(const MarketOrder& order) {
    // Validation: R3/R11 — zero quantity rejected.
    if (order.quantity == Quantity{0}) {
        return EngineResponse{EngineResult::InvalidQuantity, {}, Quantity{0}};
    }

    // Validation: R2 — duplicate OrderId (lifetime-unique per §2.1).
    if (ever_seen_ids_.contains(order.id)) {
        return EngineResponse{EngineResult::DuplicateOrderId, {}, Quantity{0}};
    }

    // Phase 8 R5 — self-trade prevention, RejectIncoming policy.
    // Pre-scan before any mutation (see submit_limit). A market order has
    // no price ceiling, so it would cross every available level — scan
    // all of them for a same-owner resting order.
    if (stp_.enabled && stp_.policy == StpPolicy::RejectIncoming &&
        would_self_cross(order.side, order.owner, std::nullopt)) {
        return EngineResponse{EngineResult::SelfTradePrevented, {},
                              Quantity{0}};
    }

    // Accept the order: record the ID as ever-seen.
    ever_seen_ids_.insert(order.id);

    // Emit on_order_accepted (R16) — emitted before matching begins.
    sink_->on_order_accepted(
        OrderAccepted{order.id, order.side, order.quantity, Price{0}});

    // Perform matching against opposite side with NO price limit (R9).
    // std::nullopt means the market order crosses all available levels.
    Quantity remaining = order.quantity;
    std::vector<Trade> trades = match_against_book(
        order.side, order.id, order.owner, remaining, std::nullopt);

    // R10: Market orders NEVER rest on the book. Any unfilled remainder
    // is simply discarded (reported in remaining_qty but not added to
    // the book). This is the "cancel remaining" behavior.
    return EngineResponse{EngineResult::Accepted, std::move(trades), remaining};
}

std::vector<Trade> MatchingEngine::match_against_book(
    Side incoming_side, OrderId incoming_id, ClientId incoming_owner,
    Quantity& remaining, std::optional<Price> limit_price) {
    std::vector<Trade> trades;

    const bool stp_cancel_resting =
        stp_.enabled && stp_.policy == StpPolicy::CancelResting;

    while (remaining > Quantity{0}) {
        // Get the best opposite-side level.
        PriceLevel* level = (incoming_side == Side::Buy)
                                ? book_.best_ask()
                                : book_.best_bid();

        if (level == nullptr) {
            break;  // No liquidity on the opposite side.
        }

        // Check crossing condition (R5):
        // Buy crosses if incoming buy price >= best ask price.
        // Sell crosses if incoming sell price <= best bid price.
        if (limit_price.has_value()) {
            if (incoming_side == Side::Buy) {
                if (limit_price.value() < level->price()) {
                    break;  // Buy price too low to cross this ask level.
                }
            } else {
                if (limit_price.value() > level->price()) {
                    break;  // Sell price too high to cross this bid level.
                }
            }
        }

        // Match against orders at this level in FIFO order.
        while (remaining > Quantity{0} && !level->empty()) {
            Order* resting = level->front();

            // Phase 8 R5 — self-trade prevention, CancelResting policy.
            // If this resting order belongs to the same owner as the
            // incoming order, pull it from the book (emitting
            // on_order_cancelled) instead of trading against it, then
            // continue matching against the next order at this level.
            // Note: the pre-scan in submit_limit/submit_market handles the
            // RejectIncoming policy before we ever reach here, so this
            // branch only fires for CancelResting.
            if (stp_cancel_resting && resting->owner == incoming_owner) {
                OrderId cancelled_id = resting->id;
                Quantity cancelled_remaining = resting->quantity;
                Side cancelled_side = resting->side;
                Price cancelled_price = resting->price;

                // remove_order unlinks from the level and, if the level
                // becomes empty, erases it from the price tree — which
                // DESTROYS the PriceLevel (it's a value in the std::map,
                // see OrderBook::remove_order). So after this call `level`
                // may be dangling; we must not touch it again. Break out
                // of the inner loop and let the outer loop re-fetch the
                // new best level via best_ask()/best_bid(). Re-fetch is
                // O(1) (map::begin), so this is cheap and always safe
                // whether or not the level survived.
                book_.remove_order(resting);
                sink_->on_order_cancelled(OrderCancelled{
                    cancelled_id, cancelled_remaining, cancelled_side,
                    cancelled_price});
                break;
            }

            // Determine fill quantity: min(remaining, resting quantity).
            Quantity fill_qty = (remaining < resting->quantity)
                                    ? remaining
                                    : resting->quantity;

            // Will this trade fully consume the resting order? Computed
            // before the quantity decrement so the publisher (Phase 6)
            // can track order count at the best price level.
            bool fully_consumed = (fill_qty == resting->quantity);

            // Build the Trade (R6: at resting order's price).
            OrderId buy_id = (incoming_side == Side::Buy)
                                 ? incoming_id
                                 : resting->id;
            OrderId sell_id = (incoming_side == Side::Sell)
                                  ? incoming_id
                                  : resting->id;

            Trade trade{
                .trade_sequence = next_trade_sequence_++,
                .buy_order_id = buy_id,
                .sell_order_id = sell_id,
                .price = resting->price,
                .quantity = fill_qty,
                .resting_order_removed = fully_consumed,
            };

            trades.push_back(trade);

            // Emit on_trade per fill (R17, R20: synchronous, before
            // EngineResponse returns).
            sink_->on_trade(trade);

            // Update quantities.
            remaining -= fill_qty;
            resting->quantity -= fill_qty;

            // Sync the level's aggregate total_qty_ with the fill.
            // Must happen BEFORE remove_order (which would subtract
            // the already-zeroed quantity and accomplish nothing).
            level->reduce_quantity(fill_qty);

            // R7: fully consumed resting order removed from book.
            if (fully_consumed) {
                book_.remove_order(resting);
            }
        }

        // Note: if the level is now empty, remove_order already handles
        // erasing the empty level from the price tree (OrderBook::remove_order
        // does this internally).
    }

    return trades;
}

bool MatchingEngine::would_self_cross(
    Side incoming_side, ClientId incoming_owner,
    std::optional<Price> limit_price) const {
    // Walk the OPPOSITE side from the best price. A buy crosses asks
    // (ascending); a sell crosses bids (descending). We reuse the const
    // price trees rather than best_ask()/best_bid() (which are non-const)
    // so this stays a pure read — no mutation, safe to call before the
    // engine records the OrderId or emits any event (NFR2).
    //
    // For each crossable level, scan its FIFO queue via the intrusive
    // Order::next chain for a resting order owned by incoming_owner.
    auto scan_level = [&](const PriceLevel& level) -> bool {
        for (const Order* o = level.front(); o != nullptr; o = o->next) {
            if (o->owner == incoming_owner) {
                return true;
            }
        }
        return false;
    };

    if (incoming_side == Side::Buy) {
        // Cross asks: lowest price first. Stop once level price exceeds
        // the limit (nullopt = market order: no ceiling, scan all).
        for (const auto& [price, level] : book_.asks()) {
            if (limit_price.has_value() && limit_price.value() < price) {
                break;  // can't cross this or any higher ask
            }
            if (scan_level(level)) {
                return true;
            }
        }
    } else {
        // Cross bids: highest price first. Stop once level price drops
        // below the limit (nullopt = market order: no floor, scan all).
        for (const auto& [price, level] : book_.bids()) {
            if (limit_price.has_value() && limit_price.value() > price) {
                break;  // can't cross this or any lower bid
            }
            if (scan_level(level)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace miniexchange
