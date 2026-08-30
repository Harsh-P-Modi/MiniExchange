#ifndef MINIEXCHANGE_CORE_ORDER_HPP
#define MINIEXCHANGE_CORE_ORDER_HPP

#include "Types.hpp"

namespace miniexchange {

// Forward declaration — PriceLevel is defined in orderbook/
class PriceLevel;

// Order — represents a resting limit order in the book.
// This is a pure data struct (no methods, per core/'s design rule).
// All linking/unlinking logic lives in orderbook/PriceLevel.
//
// Decision: intrusive doubly-linked list (prev/next embedded directly)
// rather than std::list or a separate node type. Rationale: eliminates
// per-order node allocation overhead and keeps order data contiguous
// with its queue membership metadata. See docs/LEARNING.md for full
// analysis.
//
// Note: A MarketOrder never reaches this struct — market orders match
// immediately and never rest on the book (requirements.md R9-R10).
// This struct exclusively represents limit-origin orders.
struct Order {
    OrderId id;
    Side side;
    Price price;        // the order's limit price
    Quantity quantity;  // remaining, unfilled quantity
    Sequence sequence;  // insertion order, for FIFO tiebreaking

    // owner (Phase 8): the ClientId that submitted this order, threaded
    // down from the input LimitOrder so the engine can detect self-trades
    // (STP, R5). Read only when STP is enabled — a direct field read on a
    // resting order the match loop already holds, avoiding a side-table
    // lookup. Cost: 8 bytes, which pushes Order from 64 bytes (one cache
    // line) to 72 (two cache lines). This cannot be packed back to 64 —
    // see the static_assert below and docs/LEARNING.md Phase 8 / T2.
    ClientId owner{};

    // Intrusive doubly-linked list pointers — this IS the per-price-level
    // queue. No separate node allocation, no std::list (locked decision
    // per steering/tech.md: "no std::list, ever").
    Order* prev = nullptr;
    Order* next = nullptr;

    // Back-pointer to the PriceLevel this order currently sits in.
    // Rationale: enables O(1) cancel. Given only OrderId → Order* (from
    // the owning unordered_map), we need to:
    //   (a) unlink from the intrusive list (uses prev/next), and
    //   (b) decrement that level's aggregate quantity, and
    //   (c) detect "level now empty → remove from price tree."
    // Without this back-pointer, we'd have to look up the price in the
    // tree separately, turning O(1) cancel into O(log P).
    // Cost: 8 bytes per Order. Benefit: O(1) cancel, per charter target.
    PriceLevel* level = nullptr;
};

// Pin the layout intentionally (Phase 8, design.md §1/§6). Adding the
// 8-byte `owner` took Order from exactly 64 bytes (one cache line) to 72
// (two cache lines). It cannot be packed back to 64: every remaining
// field is 8-byte-aligned, and even shrinking `Side` to a byte only
// reclaims the existing 4-byte pad, not a whole 8-byte slot. The only
// route back to one cache line is 32-bit index-based pool links instead
// of 64-bit pointers — Phase 3-style, deliberately out of scope here.
// This assert makes any accidental future size change a compile error.
static_assert(sizeof(Order) == 72,
              "Order is expected to be 72 bytes after the Phase 8 owner "
              "field; see docs/LEARNING.md Phase 8 / T2 if this changes");

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_ORDER_HPP
