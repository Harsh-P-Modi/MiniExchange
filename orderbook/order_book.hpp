#ifndef MINIEXCHANGE_ORDERBOOK_ORDER_BOOK_HPP
#define MINIEXCHANGE_ORDERBOOK_ORDER_BOOK_HPP

#include <map>
#include <unordered_map>

#include "core/Order.hpp"
#include "core/Types.hpp"
#include "orderbook/order_pool.hpp"
#include "orderbook/price_level.hpp"

namespace miniexchange {

// OrderBook — the central data structure holding all resting orders.
//
// Owns all resting Order objects via an OrderPool (fixed-capacity
// pre-allocated memory pool). The OrderIndex is a non-owning lookup
// map from OrderId → Order*. The pool provides O(1) allocation/
// deallocation without heap churn.
//
// Maintains two price trees (bids descending, asks ascending), each
// containing PriceLevel instances that manage intrusive doubly-linked
// lists of orders.
//
// This class is a pure data structure — it does NOT implement matching logic.
// The engine composes these structural primitives into its matching algorithm.
// Zero I/O, zero threading, zero knowledge of the outside world.
class OrderBook {
public:
    // Bids sorted descending: best bid = highest price = begin().
    using BidTree = std::map<Price, PriceLevel, std::greater<Price>>;
    // Asks sorted ascending: best ask = lowest price = begin().
    using AskTree = std::map<Price, PriceLevel, std::less<Price>>;
    // Non-owning index for O(1) lookup by OrderId. The pool owns lifetime.
    using OrderIndex = std::unordered_map<OrderId, Order*>;

    // Constructor: pool_capacity sets the fixed size of the backing pool.
    // Default 1,000,000 — sufficient for all Phase 1/2 usage.
    explicit OrderBook(std::size_t pool_capacity = 1'000'000);

    // Non-copyable (pool owns memory), non-movable for simplicity.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    // Insert an order into the book. Takes order data by value, acquires
    // a pool slot, copies the data in, and links into the price level.
    // Returns raw pointer to the inserted order in pool storage.
    // Returns nullptr if the pool is exhausted.
    // Precondition: order_data.id must not already exist in the book.
    Order* add_order(Order order_data);

    // Remove an order by raw pointer. Unlinks from its PriceLevel,
    // removes the level from the tree if it becomes empty, and returns
    // the slot to the pool.
    // Precondition: order must be a valid pointer owned by this book's pool.
    void remove_order(Order* order);

    // Find an order by ID. Returns nullptr if not found.
    [[nodiscard]] Order* find_order(OrderId id) const;

    // Best bid level (highest price). nullptr if no bids.
    [[nodiscard]] PriceLevel* best_bid();

    // Best ask level (lowest price). nullptr if no asks.
    [[nodiscard]] PriceLevel* best_ask();

    // Total number of resting orders in the book (both sides).
    [[nodiscard]] std::size_t order_count() const;

    // Number of free slots remaining in the pool (O(1)).
    // Used by MatchingEngine to pre-check capacity before acceptance.
    [[nodiscard]] std::size_t pool_available() const { return pool_.available(); }

    // Const access to the price trees for read-only iteration
    // (requirements.md R15: PRINT_BOOK needs full depth display).
    [[nodiscard]] const BidTree& bids() const { return bids_; }
    [[nodiscard]] const AskTree& asks() const { return asks_; }

private:
    // pool_ declared before orders_ so it outlives the index
    // (destruction order is reverse of declaration).
    OrderPool pool_;
    BidTree bids_;
    AskTree asks_;
    OrderIndex orders_;
    std::size_t order_count_ = 0;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_ORDERBOOK_ORDER_BOOK_HPP
