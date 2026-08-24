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

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_ORDER_HPP
