#ifndef MINIEXCHANGE_INTERFACES_EVENT_SINK_HPP
#define MINIEXCHANGE_INTERFACES_EVENT_SINK_HPP

#include "core/Events.hpp"
#include "core/Trade.hpp"

namespace miniexchange {

// EventSink — the output port (Observer pattern).
//
// The engine holds an injected EventSink* and calls these three methods
// for every state change, regardless of who triggered it. This is what
// lets Phase 6's UDP feed (or a benchmark counter, or a RecordingEventSink
// in tests) observe everything without the engine knowing they exist.
//
// Design decision: default no-op bodies via NullEventSink, not pure-virtual
// with empty implementations here. This keeps the ABC honestly abstract
// (no accidentally-inherited stub behaviour) while still allowing the
// engine to inject a no-op default cheaply. See docs/LEARNING.md for the
// full rationale.
//
// All three methods are pure virtual — an implementor must declare their
// intent explicitly, even if they only care about one event type. An
// override can simply forward to the base no-op NullEventSink if
// selective override is wanted; that keeps the override surface honest.
class EventSink {
public:
    virtual ~EventSink() = default;

    // Called once per individual fill, immediately after the match is
    // recorded (requirements.md R17, R20). A single incoming order that
    // crosses N resting orders produces N separate on_trade calls.
    virtual void on_trade(const Trade& trade) = 0;

    // Called once when an ADD request is accepted (requirements.md R16).
    // Emitted for both limit orders (which may then rest) and market
    // orders (which never rest but are still "accepted"). Not emitted on
    // any rejection (requirements.md R19).
    virtual void on_order_accepted(const OrderAccepted& event) = 0;

    // Called once when a resting order is successfully cancelled
    // (requirements.md R18). Not emitted when cancel fails (R19).
    virtual void on_order_cancelled(const OrderCancelled& event) = 0;
};

// NullEventSink — the no-op default EventSink.
//
// Used as the default injected into MatchingEngine when no real sink is
// wired up (unit tests that only care about EngineResponse, benchmark
// harnesses, etc.).
//
// Implemented as a Meyer's singleton: a function-local static ensures
// exactly one instance exists, is lazily constructed on first call,
// and is thread-safe without any explicit synchronisation (C++11
// guarantees). No dynamic allocation, no global constructor ordering
// issues.
class NullEventSink final : public EventSink {
public:
    // Returns the singleton instance. Never returns null.
    // Thread-safe in C++11+ (function-local static init is atomic).
    static NullEventSink* instance() {
        static NullEventSink sink;
        return &sink;
    }

    void on_trade(const Trade& /*trade*/) override {}
    void on_order_accepted(const OrderAccepted& /*event*/) override {}
    void on_order_cancelled(const OrderCancelled& /*event*/) override {}

private:
    // Private constructor — only instance() may construct.
    NullEventSink() = default;
    // Non-copyable, non-movable (singleton semantics).
    NullEventSink(const NullEventSink&) = delete;
    NullEventSink& operator=(const NullEventSink&) = delete;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_INTERFACES_EVENT_SINK_HPP
