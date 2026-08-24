#include "orderbook/price_level.hpp"

namespace miniexchange {

PriceLevel::PriceLevel(Price price) : price_(price) {}

void PriceLevel::push_back(Order* order) {
    // Set the order's back-pointer to this level.
    order->level = this;

    // Link into the doubly-linked list at the tail (FIFO: newest at back).
    order->prev = tail_;
    order->next = nullptr;

    if (tail_ != nullptr) {
        tail_->next = order;
    } else {
        // List was empty — this order is also the head.
        head_ = order;
    }
    tail_ = order;

    // Maintain aggregate quantity incrementally.
    total_qty_ += order->quantity;
}

void PriceLevel::remove(Order* order) {
    // Fix up the predecessor's next pointer (or head_ if removing the head).
    if (order->prev != nullptr) {
        order->prev->next = order->next;
    } else {
        head_ = order->next;
    }

    // Fix up the successor's prev pointer (or tail_ if removing the tail).
    if (order->next != nullptr) {
        order->next->prev = order->prev;
    } else {
        tail_ = order->prev;
    }

    // Maintain aggregate quantity incrementally.
    total_qty_ -= order->quantity;

    // Clear the removed order's linkage pointers to avoid dangling references.
    order->prev = nullptr;
    order->next = nullptr;
    order->level = nullptr;
}

void PriceLevel::reduce_quantity(Quantity qty) {
    total_qty_ -= qty;
}

Order* PriceLevel::front() const {
    return head_;
}

bool PriceLevel::empty() const {
    return head_ == nullptr;
}

Price PriceLevel::price() const {
    return price_;
}

Quantity PriceLevel::total_quantity() const {
    return total_qty_;
}

}  // namespace miniexchange
