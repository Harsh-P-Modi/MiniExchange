#ifndef MINIEXCHANGE_STRATEGY_STRATEGY_HPP
#define MINIEXCHANGE_STRATEGY_STRATEGY_HPP

#include "core/Events.hpp"
#include "core/Types.hpp"
#include "interfaces/engine_api.hpp"

namespace miniexchange {

// Strategy — the SDK interface (design.md §2). A strategy is an ordinary
// EngineAPI client: it submits orders through the same input port every
// adapter uses, and it learns about outcomes only through notifications —
// there is NO privileged read path into the book (NFR1). This proves the
// port abstraction generalizes from "human/network client" to
// "algorithmic client" without special-casing.
//
// Two notification hooks:
//   - on_response(): the outcome of THIS strategy's own submit/cancel
//     (fills against its orders, rejects, cancel-acks). Reuses the
//     EngineResponse shape (Phase 1) rather than inventing a parallel
//     type.
//   - on_trade(): a market-data trade event — ANY trade in the book,
//     including those between other participants. This is the EventSink
//     shape (Phase 1/6): it's how a momentum strategy sees the tape.
//   - on_tick(): a periodic nudge from the runner to let a strategy act
//     on a timer (e.g. a market maker placing its initial quotes, or
//     re-evaluating). Default no-op.
//
// The engine is injected by reference at construction (constructor DI,
// matching the project's convention) and the strategy is assigned a fixed
// ClientId — a strategy is a single persistent "client" for the session,
// the same role a TCP connection's ClientId plays.
class Strategy {
public:
    Strategy(EngineAPI& engine, ClientId id) : engine_(engine), id_(id) {}
    virtual ~Strategy() = default;

    // Outcome of one of this strategy's own submit()/cancel() calls.
    // Default no-op — a fire-and-forget strategy may not care.
    virtual void on_response(const EngineResponse& /*response*/) {}

    // A trade occurred in the book (market data). Default no-op.
    virtual void on_trade(const Trade& /*trade*/) {}

    // Periodic tick from the runner. Default no-op.
    virtual void on_tick() {}

    [[nodiscard]] ClientId id() const { return id_; }

protected:
    EngineAPI& engine_;
    ClientId id_;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_STRATEGY_STRATEGY_HPP
