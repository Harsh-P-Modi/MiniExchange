#ifndef MINIEXCHANGE_INTERFACES_ENGINE_API_HPP
#define MINIEXCHANGE_INTERFACES_ENGINE_API_HPP

#include "core/Events.hpp"
#include "core/NewOrder.hpp"

namespace miniexchange {

// Forward declaration — defined in orderbook/order_book.hpp.
// EngineAPI exposes a const reference to the book for read-only
// introspection (requirements.md R15: PRINT_BOOK support).
class OrderBook;

// EngineAPI — the input port (Ports & Adapters pattern).
//
// apps/* and adapters/* depend on this abstraction, never on
// MatchingEngine's concrete class directly. This enforces the
// dependency-inversion principle: the CLI, the benchmark harness, and
// later the TCP/FIX adapters are all wired to the port, so swapping the
// engine implementation (e.g. Phase 3's pooled allocator, Phase 4's
// lock-free dispatch) requires zero changes in any app or adapter.
//
// Two pure virtual methods cover the entire engine surface for Phase 1:
//   - submit(): accepts a NewOrder variant (limit or market)
//   - cancel(): removes a resting order by ID
//   - book():   read-only view for depth display / testing
//
// EngineResponse is returned synchronously to the immediate caller.
// EventSink (see event_sink.hpp) is the separate broadcast channel for
// state changes that any observer needs — the two channels are
// deliberately distinct per requirements.md §4.
class EngineAPI {
public:
    virtual ~EngineAPI() = default;

    // Submit a new order (limit or market). Returns synchronously with
    // the result of the submission: status, any trades that occurred,
    // and remaining unfilled quantity.
    //
    // Never throws for expected business outcomes (duplicate ID, invalid
    // price/quantity, etc.) — those are returned as EngineResult codes.
    virtual EngineResponse submit(const NewOrder& order) = 0;

    // Cancel a resting order by its client-supplied ID. Returns
    // Accepted on success, UnknownOrderId if the ID is not currently
    // resting (never existed, already filled, or already cancelled).
    virtual EngineResponse cancel(OrderId id) = 0;

    // Read-only access to the order book for depth display and testing
    // (requirements.md R15). The engine owns the book; this exposes a
    // non-owning const reference.
    virtual const OrderBook& book() const = 0;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_INTERFACES_ENGINE_API_HPP
