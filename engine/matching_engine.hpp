#ifndef MINIEXCHANGE_ENGINE_MATCHING_ENGINE_HPP
#define MINIEXCHANGE_ENGINE_MATCHING_ENGINE_HPP

#include <optional>
#include <unordered_set>
#include <vector>

#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include "interfaces/engine_api.hpp"
#include "interfaces/event_sink.hpp"
#include "orderbook/order_book.hpp"

namespace miniexchange {

// MatchingEngine — implements the EngineAPI input port.
//
// Owns the OrderBook, the lifetime-uniqueness set, sequence counters,
// and an injected EventSink* for broadcasting state changes. Implements
// price-time priority matching for limit orders (and later, market
// orders in Task 11).
//
// Zero I/O, zero threading, zero knowledge of CLI/TCP/FIX/UDP.
// Returns structured results for expected business outcomes (duplicate
// ID, invalid price/qty) — never throws for client-input errors.
class MatchingEngine : public EngineAPI {
public:
    // Constructor: takes an optional EventSink pointer and pool capacity.
    // Defaults to the NullEventSink singleton (no-op) when nothing is
    // wired up, and 1,000,000 pool slots (sufficient for production use;
    // tests may pass a smaller value to exercise pool exhaustion).
    explicit MatchingEngine(EventSink* sink = NullEventSink::instance(),
                            std::size_t pool_capacity = 1'000'000,
                            StpConfig stp = StpConfig{});

    // EngineAPI interface implementation.
    EngineResponse submit(const NewOrder& order) override;
    EngineResponse cancel(OrderId id) override;
    [[nodiscard]] const OrderBook& book() const override;

private:
    OrderBook book_;

    // Lifetime-uniqueness: every OrderId ever accepted by the engine,
    // regardless of whether the order is still resting. Prevents reuse.
    // Deliberately NOT part of OrderBook — this is a business rule about
    // the engine's acceptance policy, not a structural property of
    // "what's resting." (requirements.md §2.1)
    std::unordered_set<OrderId> ever_seen_ids_;

    Sequence next_sequence_{0};
    TradeSequence next_trade_sequence_{0};
    EventSink* sink_;
    StpConfig stp_;  // Phase 8: self-trade-prevention config (default disabled)

    // Dispatch helpers — called by submit() via std::visit.
    EngineResponse submit_limit(const LimitOrder& order);
    EngineResponse submit_market(const MarketOrder& order);

    // Shared matching loop: consumes opposite-side liquidity starting
    // at best price, up to limit_price (nullopt for market orders —
    // no ceiling/floor) or until remaining hits 0.
    // Modifies `remaining` in place. Returns the vector of trades.
    //
    // incoming_owner (Phase 8): used only when STP is enabled with the
    // CancelResting policy — a resting order with this owner is pulled
    // from the book (emitting on_order_cancelled) instead of traded
    // against, and matching continues past it.
    std::vector<Trade> match_against_book(
        Side incoming_side, OrderId incoming_id, ClientId incoming_owner,
        Quantity& remaining, std::optional<Price> limit_price);

    // STP pre-scan (Phase 8, RejectIncoming policy): returns true if the
    // incoming order would cross any resting order owned by
    // incoming_owner, walking the opposite side from best price up to
    // limit_price (nullopt = market order, scans all crossable levels).
    // Pure read — no mutation — so it can run BEFORE the engine records
    // the OrderId or emits any event, preserving the zero-side-effect
    // contract (NFR2). See design.md §5.
    [[nodiscard]] bool would_self_cross(
        Side incoming_side, ClientId incoming_owner,
        std::optional<Price> limit_price) const;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_ENGINE_MATCHING_ENGINE_HPP
