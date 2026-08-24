#ifndef MINIEXCHANGE_ORDERBOOK_PRICE_LEVEL_HPP
#define MINIEXCHANGE_ORDERBOOK_PRICE_LEVEL_HPP

#include "core/Order.hpp"
#include "core/Types.hpp"

namespace miniexchange {

// PriceLevel — manages a FIFO queue of resting orders at a single price.
//
// The queue is implemented as an intrusive doubly-linked list using the
// Order::prev / Order::next pointers directly (no separate node
// allocation, no std::list). This class owns all linking/unlinking logic;
// Order itself is a pure data struct with no methods (per core/'s rule).
//
// PriceLevel does NOT own the Order objects — ownership lives in
// OrderBook's unordered_map<OrderId, unique_ptr<Order>>. PriceLevel
// merely links/unlinks non-owning raw pointers into its queue.
//
// total_quantity_ is maintained incrementally on every push/remove,
// making total_quantity() an O(1) read with zero traversal.
class PriceLevel {
public:
    explicit PriceLevel(Price price);

    // Append an order to the back of the FIFO queue. O(1).
    // Precondition: order is not already in any PriceLevel's queue.
    void push_back(Order* order);

    // Unlink an order from this queue. O(1).
    // Precondition: order->level == this (i.e., the order is actually in this level).
    void remove(Order* order);

    // Return the front (oldest) order in the queue. nullptr if empty.
    [[nodiscard]] Order* front() const;

    // Decrement total_qty_ by qty without unlinking any order.
    // Used by the matching loop when a resting order is partially filled
    // (its quantity field was decremented directly, but the level's
    // aggregate must stay in sync).
    void reduce_quantity(Quantity qty);

    // True if the queue contains no orders.
    [[nodiscard]] bool empty() const;

    // The price this level represents.
    [[nodiscard]] Price price() const;

    // Total resting quantity across all orders at this level. O(1) read.
    [[nodiscard]] Quantity total_quantity() const;

private:
    Price price_;
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
    Quantity total_qty_{0};
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_ORDERBOOK_PRICE_LEVEL_HPP
