#ifndef MINIEXCHANGE_CORE_NEWORDER_HPP
#define MINIEXCHANGE_CORE_NEWORDER_HPP

#include "Types.hpp"
#include <variant>

namespace miniexchange {

// LimitOrder — input representation of a limit order submission.
// Structurally identical to the resting Order (minus sequence/pointers,
// which the engine assigns), but kept as a separate type to distinguish
// "what the client submits" from "what rests on the book."
struct LimitOrder {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};

// MarketOrder — input representation of a market order submission.
// Critically: no price field. This makes "a market order with a price"
// compile-time-impossible, not just a runtime validation error.
// Requirements R4/R11 become structural guarantees rather than checks.
struct MarketOrder {
    OrderId id;
    Side side;
    Quantity quantity;  // no price — structurally enforced
};

// NewOrder — the discriminated union passed to EngineAPI::submit.
// Decision: std::variant<LimitOrder, MarketOrder> over a flat struct
// with an OrderType enum and an optionally-ignored price field.
// Rationale: "market order with a price" becomes a compile-time error
// rather than a runtime rule. The cost is std::visit boilerplate at
// the one call site (MatchingEngine::submit) that dispatches on type;
// the benefit is that requirements.md R4/R11 ("Market orders carry no
// price") is enforced by the type system, not by the engine checking
// and rejecting at runtime.
using NewOrder = std::variant<LimitOrder, MarketOrder>;

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_NEWORDER_HPP
