#ifndef MINIEXCHANGE_ORDERBOOK_ORDER_BOOK_HPP
#define MINIEXCHANGE_ORDERBOOK_ORDER_BOOK_HPP

#include <map>
#include <memory>
#include <unordered_map>

#include "core/Order.hpp"
#include "core/Types.hpp"
#include "orderbook/price_level.hpp"

namespace miniexchange {

// OrderBook — the central data structure holding all resting orders.
//
// Owns all resting Order objects via unordered_map<OrderId, unique_ptr<Order>>.
// Maintains two price trees (bids descending, asks ascending), each containing
// PriceLevel instances that manage intrusive doubly-linked lists of orders.
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
    // Sole owner of every resting Order's lifetime.
    using OrderOwnerMap = std::unordered_map<OrderId, std::unique_ptr<Order>>;

    OrderBook() = default;

    // Non-copyable (owns unique_ptrs), non-movable for simplicity.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    // Insert an order into the book. Takes ownership via unique_ptr.
    // Creates a new price level if one doesn't exist at this price.
    // Returns raw (non-owning) pointer to the inserted order.
    // Precondition: order->id must not already exist in the book.
    Order* add_order(std::unique_ptr<Order> order);

    // Remove an order by raw pointer. Unlinks from its PriceLevel,
    // removes the level from the tree if it becomes empty, and destroys
    // the Order (erases from the owning map).
    // Precondition: order must be a valid pointer owned by this book.
    void remove_order(Order* order);

    // Find an order by ID. Returns nullptr if not found.
    [[nodiscard]] Order* find_order(OrderId id) const;

    // Best bid level (highest price). nullptr if no bids.
    [[nodiscard]] PriceLevel* best_bid();

    // Best ask level (lowest price). nullptr if no asks.
    [[nodiscard]] PriceLevel* best_ask();

    // Total number of resting orders in the book (both sides).
    [[nodiscard]] std::size_t order_count() const;

    // Const access to the price trees for read-only iteration
    // (requirements.md R15: PRINT_BOOK needs full depth display).
    [[nodiscard]] const BidTree& bids() const { return bids_; }
    [[nodiscard]] const AskTree& asks() const { return asks_; }

private:
    BidTree bids_;
    AskTree asks_;
    OrderOwnerMap orders_;
    std::size_t order_count_ = 0;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_ORDERBOOK_ORDER_BOOK_HPP
