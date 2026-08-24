#include "orderbook/order_book.hpp"

namespace miniexchange {

Order* OrderBook::add_order(std::unique_ptr<Order> order) {
    Order* raw = order.get();
    const Price price = raw->price;
    const Side side = raw->side;
    const OrderId id = raw->id;

    // Transfer ownership into the owning map.
    orders_.emplace(id, std::move(order));

    // Select the correct price tree based on order side.
    if (side == Side::Buy) {
        // emplace with piecewise_construct: creates the PriceLevel in-place
        // if it doesn't exist yet. If it does exist, this is a no-op and
        // returns an iterator to the existing level.
        auto [it, inserted] = bids_.try_emplace(price, price);
        it->second.push_back(raw);
    } else {
        auto [it, inserted] = asks_.try_emplace(price, price);
        it->second.push_back(raw);
    }

    ++order_count_;
    return raw;
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

    // Destroy the order by erasing from the owning map.
    orders_.erase(order->id);
    --order_count_;
}

Order* OrderBook::find_order(OrderId id) const {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return nullptr;
    }
    return it->second.get();
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
