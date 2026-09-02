#ifndef MINIEXCHANGE_ENGINE_MATCHING_ENGINE_HPP
#define MINIEXCHANGE_ENGINE_MATCHING_ENGINE_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
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
// Owns the OrderBook, the per-client monotonic-ID watermark (Phase 11
// R7), sequence counters, and an injected EventSink* for broadcasting
// state changes. Implements
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

    // Phase 11 R7 — diagnostic: how many distinct ClientIds currently
    // have a monotonic-ID watermark recorded. This is the size of the
    // replacement structure for the old unbounded ever_seen_ids_ set;
    // it is bounded by concurrent-client count, NOT by lifetime order
    // count. Exposed (like book().order_count()) so the R7 stress test
    // can assert the bound directly. Not part of the EngineAPI port.
    [[nodiscard]] std::size_t tracked_client_count() const {
        return last_accepted_id_.size();
    }

private:
    OrderBook book_;

    // Phase 11 R7 — per-client monotonic-ID watermark. Replaces the
    // former std::unordered_set<OrderId> ever_seen_ids_, which retained
    // one entry for every order ever accepted, forever (unbounded: it
    // grew with lifetime order count, never with anything that shrinks).
    //
    // New rule: for each ClientId, the highest OrderId that client has so
    // far had accepted. A submit whose id is <= that client's watermark
    // is rejected as DuplicateOrderId. This is O(1) per check and per
    // update, and the map's size is bounded by the number of distinct
    // clients — not by order count.
    //
    // This is a deliberate, documented SEMANTIC NARROWING (requirements.md
    // §7): uniqueness is now per-client and monotonic ("your own ids must
    // strictly increase"), not global-lifetime ("no id may ever repeat
    // across any client"). The wire contract is unchanged — a violation
    // still returns EngineResult::DuplicateOrderId. Still NOT part of
    // OrderBook: an acceptance-policy rule, not a structural property of
    // what is resting.
    std::unordered_map<ClientId, OrderId> last_accepted_id_;

    // Phase 11 R8 — per-client resting-order price index, for O(1)
    // self-trade prevention. For each ClientId, an ordered count of the
    // prices at which that client currently has resting bids / asks
    // (price -> how many of this client's orders rest there). Maintained
    // incrementally wherever the engine adds or removes a resting order
    // (both already O(1) in OrderBook); a std::map keeps the extremes
    // (begin()/rbegin()) available in O(1) so would_self_cross becomes a
    // single lookup + compare instead of a walk of the crossable side.
    //
    // Only touched when stp_.enabled — an engine with STP off (the
    // default) pays nothing here (NFR3: no new hot-path cost for the
    // common configuration).
    struct ClientRestingPrices {
        std::map<Price, std::uint32_t> bids;  // ascending; max = rbegin()
        std::map<Price, std::uint32_t> asks;  // ascending; min = begin()
    };
    std::unordered_map<ClientId, ClientRestingPrices> client_resting_;

    Sequence next_sequence_{0};
    TradeSequence next_trade_sequence_{0};
    EventSink* sink_;
    StpConfig stp_;  // Phase 8: self-trade-prevention config (default disabled)

    // Dispatch helpers — called by submit() via std::visit.
    EngineResponse submit_limit(const LimitOrder& order);
    EngineResponse submit_market(const MarketOrder& order);

    // Phase 11 R8 — maintain client_resting_ alongside the book. Called
    // adjacent to every OrderBook::add_order / remove_order the engine
    // performs on a resting order. No-ops when stp_.enabled is false.
    void index_rest(const Order& resting);
    void index_unrest(const Order& resting);

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

    // STP check (Phase 8, RejectIncoming policy): returns true if the
    // incoming order would cross any resting order owned by
    // incoming_owner. limit_price nullopt = market order (crosses every
    // opposite level). Pure read — no mutation — so it can run BEFORE the
    // engine records the OrderId or emits any event, preserving the
    // zero-side-effect contract (NFR2). See design.md §5.
    //
    // Phase 11 R8: this used to walk the crossable opposite side, O(book
    // depth). It now consults client_resting_ — the incoming owner's best
    // opposite-side resting price — so it is O(1) regardless of how deep
    // the book is or how far a market order would sweep.
    [[nodiscard]] bool would_self_cross(
        Side incoming_side, ClientId incoming_owner,
        std::optional<Price> limit_price) const;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_ENGINE_MATCHING_ENGINE_HPP
