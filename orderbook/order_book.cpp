#include "orderbook/order_book.hpp"

namespace miniexchange {

OrderBook::OrderBook(std::size_t pool_capacity) : pool_(pool_capacity) {}

Order* OrderBook::add_order(Order order_data) {
    // Acquire a slot from the pool (O(1) free-list pop).
    Order* slot = pool_.acquire();
    if (slot == nullptr) {
        return nullptr;  // pool exhausted — caller handles this
    }

    // Copy order data into the pool slot.
    *slot = order_data;

    const Price price = slot->price;
    const Side side = slot->side;
    const OrderId id = slot->id;

    // Insert raw pointer into the non-owning index.
    orders_.emplace(id, slot);

    // Select the correct price tree based on order side.
    if (side == Side::Buy) {
        auto [it, inserted] = bids_.try_emplace(price, price);
        it->second.push_back(slot);
    } else {
        auto [it, inserted] = asks_.try_emplace(price, price);
        it->second.push_back(slot);
    }

    ++order_count_;
    return slot;
}

void OrderBook::remove_order(Order* order) {
    // Unlink from the intrusive list in its PriceLevel.
    PriceLevel* level = order->level;
    const Price price = order->price;
    const Side side = order->side;

    level->remove(order);

    // If the level is now empty, remove it from the price tree.
    if (level->empty()) {
        if (side == Side::Buy) {
            bids_.erase(price);
        } else {
            asks_.erase(price);
        }
    }

    // Erase from the non-owning index, then release slot back to pool.
    orders_.erase(order->id);
    pool_.release(order);
    --order_count_;
}

Order* OrderBook::find_order(OrderId id) const {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return nullptr;
    }
    return it->second;
}

PriceLevel* OrderBook::best_bid() {
    if (bids_.empty()) {
        return nullptr;
    }
    return &bids_.begin()->second;
}

PriceLevel* OrderBook::best_ask() {
    if (asks_.empty()) {
        return nullptr;
    }
    return &asks_.begin()->second;
}

std::size_t OrderBook::order_count() const {
    return order_count_;
}

}  // namespace miniexchange
