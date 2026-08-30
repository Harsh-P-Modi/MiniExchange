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

// StpPolicy (Phase 8) — how self-trade prevention resolves a would-be
// self-cross (an incoming order about to trade against a resting order
// owned by the same ClientId). RejectIncoming is the conservative
// default; CancelResting mirrors venues that pull the resting order and
// let the aggressor proceed. Lives in core/ (not risk/) because the
// engine executes STP inside its match loop and must not depend on the
// risk layer — see specs/phase-08-risk-engine/design.md §5.
enum class StpPolicy { RejectIncoming, CancelResting };

// StpConfig (Phase 8) — the slice of risk configuration the engine needs
// to perform STP. Passed into MatchingEngine at construction. Defaults
// to disabled, so an engine constructed without risk config behaves
// exactly as it did before Phase 8 (no STP checks, no behavior change).
struct StpConfig {
    bool enabled = false;
    StpPolicy policy = StpPolicy::RejectIncoming;
};

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

struct SymbolId {
    uint32_t value;

    constexpr SymbolId() : value(0) {}
    explicit constexpr SymbolId(uint32_t v) : value(v) {}

    constexpr bool operator==(const SymbolId& other) const {
        return value == other.value;
    }
    constexpr bool operator!=(const SymbolId& other) const {
        return value != other.value;
    }
};

}  // namespace miniexchange

// Hash support for domain primitives (needed for unordered_map/unordered_set)
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

template <>
struct hash<miniexchange::SymbolId> {
    std::size_t operator()(const miniexchange::SymbolId& id) const noexcept {
        return std::hash<uint32_t>{}(id.value);
    }
};
}  // namespace std

#endif  // MINIEXCHANGE_CORE_TYPES_HPP
