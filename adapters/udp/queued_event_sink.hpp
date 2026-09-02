#ifndef MINIEXCHANGE_ADAPTERS_UDP_QUEUED_EVENT_SINK_HPP
#define MINIEXCHANGE_ADAPTERS_UDP_QUEUED_EVENT_SINK_HPP

// queued_event_sink.hpp — Phase 11 R5.
//
// The matching engine calls its injected EventSink* synchronously, on
// its own call stack, for every state change. In the production
// composition root that EventSink is UdpFeedPublisher, whose handlers
// call sendto() — so an order that crosses several resting orders drives
// several blocking syscalls before submit() even returns. The steering
// rule "the engine performs zero I/O" (.kiro/steering/tech.md) is
// violated in spirit: I/O happens inside the one synchronous call.
//
// QueuedEventSink is a decorator that sits between the engine and the
// real publisher. It implements the same EventSink port the engine
// already calls, but instead of doing I/O it copies a small POD
// (FeedEvent) into an SPSC ring and returns. A separate thread owns the
// real UdpFeedPublisher, drains the ring, and does the sendto() calls
// from ITS stack. The engine's call stack returns from submit()/cancel()
// without ever having waited on a socket.
//
// Backpressure: if the ring is full, the event is dropped and a counter
// is bumped. A stale/missed market-data tick is the correct failure mode
// for a feed (the research report's market-data section agrees) — what
// matters is that drops are *counted*, not that they never happen. The
// engine thread is never blocked waiting for feed-queue space.
//
// This file is platform-neutral: SpscRingBuffer is header-only,
// UdpFeedPublisher builds everywhere (it stubs sendto on non-Linux), so
// QueuedEventSink and its drain loop are unit-testable on any platform.
// Only the std::thread that runs the drain loop is spawned from the
// Linux-only exchange_server composition root.

#include <atomic>
#include <cstdint>
#include <thread>

#include "core/Events.hpp"
#include "core/Trade.hpp"
#include "interfaces/event_sink.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

namespace miniexchange::udp {

// FeedEvent — a discriminated POD carrying one of the three EventSink
// payloads across the ring. Deliberately a plain aggregate of trivially
// copyable members (no union, no user-declared special members) so
// SpscRingBuffer's std::array<FeedEvent, N> storage and its by-value
// try_push both work with zero fuss. Only the member named by `kind` is
// meaningful; the others are left default-constructed.
struct FeedEvent {
    enum class Kind : uint8_t { Trade, OrderAccepted, OrderCancelled };

    Kind kind{Kind::Trade};
    Trade trade{};
    OrderAccepted accepted{};
    OrderCancelled cancelled{};

    static FeedEvent from_trade(const Trade& t) {
        FeedEvent e;
        e.kind = Kind::Trade;
        e.trade = t;
        return e;
    }
    static FeedEvent from_accepted(const OrderAccepted& a) {
        FeedEvent e;
        e.kind = Kind::OrderAccepted;
        e.accepted = a;
        return e;
    }
    static FeedEvent from_cancelled(const OrderCancelled& c) {
        FeedEvent e;
        e.kind = Kind::OrderCancelled;
        e.cancelled = c;
        return e;
    }
};

// Replay a dequeued FeedEvent into a real EventSink (the drain thread's
// UdpFeedPublisher). Kept as a free function so it can be reused by
// tests that assert "the queued path produces the same publisher state
// as the direct path."
inline void apply(EventSink& target, const FeedEvent& e) {
    switch (e.kind) {
        case FeedEvent::Kind::Trade:
            target.on_trade(e.trade);
            return;
        case FeedEvent::Kind::OrderAccepted:
            target.on_order_accepted(e.accepted);
            return;
        case FeedEvent::Kind::OrderCancelled:
            target.on_order_cancelled(e.cancelled);
            return;
    }
}

// QueuedEventSink — the engine-thread-side EventSink. Push-only: every
// handler serializes its payload into a FeedEvent and offers it to the
// ring, dropping (and counting) on full. Never blocks, never does I/O.
//
// Capacity defaults to 16384 slots (a few MiB of FeedEvent storage);
// tests pass a small power of two to exercise the drop path.
template <std::size_t Capacity = 16384>
class QueuedEventSink final : public EventSink {
public:
    void on_trade(const Trade& trade) override {
        offer(FeedEvent::from_trade(trade));
    }
    void on_order_accepted(const OrderAccepted& event) override {
        offer(FeedEvent::from_accepted(event));
    }
    void on_order_cancelled(const OrderCancelled& event) override {
        offer(FeedEvent::from_cancelled(event));
    }

    // Consumer side — called only by the drain thread.
    bool try_pop(FeedEvent& out) { return queue_.try_pop(out); }

    // Diagnostics. dropped(): events discarded because the ring was full.
    [[nodiscard]] uint64_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t pending() const { return queue_.size(); }
    static constexpr std::size_t capacity() { return Capacity; }

private:
    void offer(FeedEvent e) {
        if (!queue_.try_push(e)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    SpscRingBuffer<FeedEvent, Capacity> queue_;
    std::atomic<uint64_t> dropped_{0};
};

// Drain loop body: pop everything currently in the sink's ring and
// replay it into `real`. Returns the number of events drained. Does no
// blocking beyond `real`'s own sendto (which uses MSG_DONTWAIT / drops
// on EWOULDBLOCK). Called in a loop by the feed-publisher thread.
template <std::size_t Capacity>
std::size_t drain_once(QueuedEventSink<Capacity>& sink, EventSink& real) {
    std::size_t n = 0;
    FeedEvent e;
    while (sink.try_pop(e)) {
        apply(real, e);
        ++n;
    }
    return n;
}

// The feed-publisher thread's whole body: drain, yield, repeat until
// `stop` is set, then do one final drain so nothing queued right before
// shutdown is lost. Spawned as a std::thread by the composition root.
template <std::size_t Capacity>
void run_feed_publisher(QueuedEventSink<Capacity>& sink, EventSink& real,
                        std::atomic<bool>& stop) {
    while (!stop.load(std::memory_order_relaxed)) {
        if (drain_once(sink, real) == 0) {
            std::this_thread::yield();
        }
    }
    drain_once(sink, real);
}

}  // namespace miniexchange::udp

#endif  // MINIEXCHANGE_ADAPTERS_UDP_QUEUED_EVENT_SINK_HPP
