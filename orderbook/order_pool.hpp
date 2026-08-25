#ifndef MINIEXCHANGE_ORDERBOOK_ORDER_POOL_HPP
#define MINIEXCHANGE_ORDERBOOK_ORDER_POOL_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "core/Order.hpp"

namespace miniexchange {

// OrderPool — fixed-capacity, pre-allocated pool of Order slots.
// Acquire/release are O(1) via an intrusive free list stored within
// unused slots themselves (reusing the first sizeof(size_t) bytes of
// each free slot to hold the index of the next free slot).
//
// Once allocated at construction, the backing storage never moves or
// reallocates — stable addresses for intrusive pointers in PriceLevel
// queues (Order::prev/next/level remain valid as long as the pool lives).
class OrderPool {
public:
    explicit OrderPool(std::size_t capacity);
    ~OrderPool();

    // Non-copyable, non-movable (addresses must remain stable).
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;
    OrderPool(OrderPool&&) = delete;
    OrderPool& operator=(OrderPool&&) = delete;

    // Returns a pointer to an uninitialized Order slot, or nullptr if
    // the pool is exhausted. O(1) — free-list pop.
    Order* acquire();

    // Returns a previously-acquired Order slot to the free list. O(1).
    // Precondition: order must be a pointer this pool previously handed
    // out via acquire(), and must not have been released already.
    void release(Order* order);

    std::size_t capacity() const { return capacity_; }
    std::size_t available() const { return free_count_; }

private:
    Order* storage_;          // raw allocation, never default-constructed
    std::size_t capacity_;
    std::size_t free_list_head_;  // index into storage_, or capacity_ = empty sentinel
    std::size_t free_count_;

    // Helper: read the "next free index" stored in a free slot's memory.
    // Safe because a free slot is not a valid Order — nobody holds a
    // pointer to it after release().
    std::size_t& next_free(std::size_t index);
    const std::size_t& next_free(std::size_t index) const;

    // Helper: convert an Order* back to its index in storage_.
    std::size_t index_of(const Order* order) const;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_ORDERBOOK_ORDER_POOL_HPP
