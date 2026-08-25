#include "orderbook/order_pool.hpp"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace miniexchange {

// Compile-time guarantee: an Order slot must be large enough to store
// the free-list link (a size_t index) while the slot is unused.
static_assert(sizeof(Order) >= sizeof(std::size_t),
              "Order must be at least sizeof(size_t) to hold the free-list index");

// Also ensure proper alignment so reinterpret_cast to size_t is safe.
static_assert(alignof(Order) >= alignof(std::size_t),
              "Order alignment must be at least alignof(size_t)");

OrderPool::OrderPool(std::size_t capacity)
    : storage_(nullptr), capacity_(capacity), free_list_head_(0), free_count_(capacity) {
    if (capacity > 0) {
        // Allocate raw memory — no constructors called. Free slots are not
        // valid Order objects; their memory is reused for the free-list link.
        // This avoids requiring Order (with its strong-typed fields) to be
        // default-constructible.
        void* raw = ::operator new(capacity * sizeof(Order), std::align_val_t{alignof(Order)});
        storage_ = static_cast<Order*>(raw);

        // Initialize the free list: chain 0 -> 1 -> 2 -> ... -> (capacity-1) -> sentinel.
        for (std::size_t i = 0; i < capacity; ++i) {
            next_free(i) = i + 1;  // last slot gets capacity_ (the "empty" sentinel)
        }
    }
}

OrderPool::~OrderPool() {
    if (storage_) {
        // No destructors to call — Order is a trivial struct (POD-like,
        // no user-defined destructor). Just free the raw memory.
        ::operator delete(storage_, capacity_ * sizeof(Order), std::align_val_t{alignof(Order)});
    }
}

Order* OrderPool::acquire() {
    if (free_list_head_ == capacity_) {
        return nullptr;  // pool exhausted
    }

    std::size_t slot = free_list_head_;
    free_list_head_ = next_free(slot);
    --free_count_;

    return &storage_[slot];
}

void OrderPool::release(Order* order) {
    std::size_t idx = index_of(order);
    assert(idx < capacity_ && "release(): order pointer is outside pool storage range");

    // Push this slot onto the front of the free list.
    next_free(idx) = free_list_head_;
    free_list_head_ = idx;
    ++free_count_;
}

std::size_t& OrderPool::next_free(std::size_t index) {
    // Reinterpret the Order slot's memory as a size_t for the free-list link.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<std::size_t*>(&storage_[index]);
}

const std::size_t& OrderPool::next_free(std::size_t index) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<const std::size_t*>(&storage_[index]);
}

std::size_t OrderPool::index_of(const Order* order) const {
    return static_cast<std::size_t>(order - storage_);
}

}  // namespace miniexchange
