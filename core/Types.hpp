#ifndef MINIEXCHANGE_CORE_TYPES_HPP
#define MINIEXCHANGE_CORE_TYPES_HPP

#include <cstdint>
#include <functional>

namespace miniexchange {

// Strong type wrappers for domain primitives.
// Decision: thin wrapper structs over bare type aliases for compile-time
// type safety — prevents accidental Price-to-Quantity mixing at zero
// runtime cost. See docs/LEARNING.md for full rationale.

struct OrderId {
    uint64_t value;

    constexpr OrderId() : value(0) {}
    explicit constexpr OrderId(uint64_t v) : value(v) {}

    // Comparison operators for container usage (unordered_map, etc.)
    constexpr bool operator==(const OrderId& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const OrderId& other) const {
        return value != other.value;
    }
};

struct ClientId {
    uint64_t value;

    constexpr ClientId() : value(0) {}
    explicit constexpr ClientId(uint64_t v) : value(v) {}

    // Comparison operators for container usage (unordered_map, etc.)
    constexpr bool operator==(const ClientId& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const ClientId& other) const {
        return value != other.value;
    }
};

struct Price {
    int64_t value;  // signed — allows negative spread calculations later,
                    // though resting orders must have value > 0

    constexpr Price() : value(0) {}
    explicit constexpr Price(int64_t v) : value(v) {}

    constexpr bool operator==(const Price& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const Price& other) const {
        return value != other.value;
    }
    constexpr bool operator<(const Price& other) const {
        return value < other.value;
    }
    constexpr bool operator<=(const Price& other) const {
        return value <= other.value;
    }
    constexpr bool operator>(const Price& other) const {
        return value > other.value;
    }
    constexpr bool operator>=(const Price& other) const {
        return value >= other.value;
    }
};

struct Quantity {
    uint64_t value;

    constexpr Quantity() : value(0) {}
    explicit constexpr Quantity(uint64_t v) : value(v) {}

    constexpr bool operator==(const Quantity& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const Quantity& other) const {
        return value != other.value;
    }
    constexpr bool operator<(const Quantity& other) const {
        return value < other.value;
    }
    constexpr bool operator<=(const Quantity& other) const {
        return value <= other.value;
    }
    constexpr bool operator>(const Quantity& other) const {
        return value > other.value;
    }
    constexpr bool operator>=(const Quantity& other) const {
        return value >= other.value;
    }

    // Arithmetic for matching logic (remaining quantity updates)
    constexpr Quantity operator-(const Quantity& other) const {
        return Quantity(value - other.value);
    }
    constexpr Quantity& operator-=(const Quantity& other) {
        value -= other.value;
        return *this;
    }
    constexpr Quantity operator+(const Quantity& other) const {
        return Quantity(value + other.value);
    }
    constexpr Quantity& operator+=(const Quantity& other) {
        value += other.value;
        return *this;
    }
};

enum class Side { Buy, Sell };

struct Sequence {
    uint64_t value;

    constexpr Sequence() : value(0) {}
    explicit constexpr Sequence(uint64_t v) : value(v) {}

    constexpr bool operator==(const Sequence& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const Sequence& other) const {
        return value != other.value;
    }
    constexpr bool operator<(const Sequence& other) const {
        return value < other.value;
    }

    // Pre-increment for engine's next_sequence_++
    constexpr Sequence& operator++() {
        ++value;
        return *this;
    }
    constexpr Sequence operator++(int) {
        Sequence tmp = *this;
        ++value;
        return tmp;
    }
};

struct TradeSequence {
    uint64_t value;

    constexpr TradeSequence() : value(0) {}
    explicit constexpr TradeSequence(uint64_t v) : value(v) {}

    constexpr bool operator==(const TradeSequence& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const TradeSequence& other) const {
        return value != other.value;
    }
    constexpr bool operator<(const TradeSequence& other) const {
        return value < other.value;
    }

    // Pre-increment for engine's next_trade_sequence_++
    constexpr TradeSequence& operator++() {
        ++value;
        return *this;
    }
    constexpr TradeSequence operator++(int) {
        TradeSequence tmp = *this;
        ++value;
        return tmp;
    }
};

}  // namespace miniexchange

// Hash support for OrderId (needed for unordered_map/unordered_set)
namespace std {
template <>
struct hash<miniexchange::OrderId> {
    std::size_t operator()(const miniexchange::OrderId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};

template <>
struct hash<miniexchange::ClientId> {
    std::size_t operator()(const miniexchange::ClientId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};
}  // namespace std

#endif  // MINIEXCHANGE_CORE_TYPES_HPP
