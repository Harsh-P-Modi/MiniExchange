// Phase 11 T5 (R5) — QueuedEventSink decouples feed publication from the
// matching call stack. These tests are platform-neutral (SpscRingBuffer
// is header-only; UdpFeedPublisher stubs sendto off Linux), so they run
// in the normal ctest suite; only the std::thread that runs the drain
// loop is spawned from the Linux-only exchange_server app.

#include "adapters/udp/queued_event_sink.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "adapters/udp/udp_feed_publisher.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/event_sink.hpp"

using namespace miniexchange;
using namespace miniexchange::udp;

namespace {

// Records every EventSink call for assertions.
class RecordingSink : public EventSink {
public:
    void on_trade(const Trade& t) override { trades.push_back(t); }
    void on_order_accepted(const OrderAccepted& a) override {
        accepted.push_back(a);
    }
    void on_order_cancelled(const OrderCancelled& c) override {
        cancelled.push_back(c);
    }
    std::vector<Trade> trades;
    std::vector<OrderAccepted> accepted;
    std::vector<OrderCancelled> cancelled;
};

// Counts EventSink calls — stands in for "did the engine thread do I/O?"
class CountingSink : public EventSink {
public:
    void on_trade(const Trade&) override { ++calls; }
    void on_order_accepted(const OrderAccepted&) override { ++calls; }
    void on_order_cancelled(const OrderCancelled&) override { ++calls; }
    int calls = 0;
};

Trade mk_trade(uint64_t seq) {
    return Trade{TradeSequence{seq}, OrderId{1}, OrderId{2},
                 Price{100}, Quantity{5}, false};
}

// ---------------------------------------------------------------------------
// Unit: push / pop / discriminant fidelity.
// ---------------------------------------------------------------------------

TEST(QueuedEventSinkTest, PushPopPreservesKindAndFields) {
    QueuedEventSink<8> sink;
    sink.on_trade(mk_trade(7));
    sink.on_order_accepted(
        OrderAccepted{OrderId{42}, Side::Buy, Quantity{9}, Price{101}});
    sink.on_order_cancelled(
        OrderCancelled{OrderId{43}, Quantity{3}, Side::Sell, Price{102}});

    FeedEvent e;
    ASSERT_TRUE(sink.try_pop(e));
    EXPECT_EQ(e.kind, FeedEvent::Kind::Trade);
    EXPECT_EQ(e.trade.trade_sequence, TradeSequence{7});

    ASSERT_TRUE(sink.try_pop(e));
    EXPECT_EQ(e.kind, FeedEvent::Kind::OrderAccepted);
    EXPECT_EQ(e.accepted.id, OrderId{42});
    EXPECT_EQ(e.accepted.price, Price{101});

    ASSERT_TRUE(sink.try_pop(e));
    EXPECT_EQ(e.kind, FeedEvent::Kind::OrderCancelled);
    EXPECT_EQ(e.cancelled.id, OrderId{43});
    EXPECT_EQ(e.cancelled.side, Side::Sell);

    EXPECT_FALSE(sink.try_pop(e));
}

// ---------------------------------------------------------------------------
// Unit: full ring drops and counts, engine thread never blocks.
// ---------------------------------------------------------------------------

TEST(QueuedEventSinkTest, DropsAndCountsWhenRingFull) {
    QueuedEventSink<4> sink;  // 4 slots
    for (int i = 0; i < 4; ++i) sink.on_trade(mk_trade(static_cast<uint64_t>(i)));
    EXPECT_EQ(sink.dropped(), 0u);
    EXPECT_EQ(sink.pending(), 4u);

    sink.on_trade(mk_trade(99));   // full -> dropped
    sink.on_trade(mk_trade(100));  // full -> dropped
    EXPECT_EQ(sink.dropped(), 2u);
    EXPECT_EQ(sink.pending(), 4u);

    // Draining frees space; further pushes succeed again.
    FeedEvent e;
    ASSERT_TRUE(sink.try_pop(e));
    sink.on_trade(mk_trade(5));
    EXPECT_EQ(sink.dropped(), 2u);
    EXPECT_EQ(sink.pending(), 4u);
}

// ---------------------------------------------------------------------------
// Unit: drain_once replays queued events into a real EventSink in order.
// ---------------------------------------------------------------------------

TEST(QueuedEventSinkTest, DrainOnceReplaysInFifoOrder) {
    QueuedEventSink<16> sink;
    RecordingSink real;

    sink.on_order_accepted(
        OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    sink.on_trade(mk_trade(1));
    sink.on_trade(mk_trade(2));
    sink.on_order_cancelled(
        OrderCancelled{OrderId{1}, Quantity{0}, Side::Buy, Price{100}});

    EXPECT_EQ(drain_once(sink, real), 4u);
    EXPECT_EQ(real.accepted.size(), 1u);
    ASSERT_EQ(real.trades.size(), 2u);
    EXPECT_EQ(real.trades[0].trade_sequence, TradeSequence{1});
    EXPECT_EQ(real.trades[1].trade_sequence, TradeSequence{2});
    EXPECT_EQ(real.cancelled.size(), 1u);

    EXPECT_EQ(drain_once(sink, real), 0u);  // nothing left
}

// ---------------------------------------------------------------------------
// Integration: an engine wired to QueuedEventSink does NOT touch the real
// sink synchronously — submit() returns having done only the ring push.
// ---------------------------------------------------------------------------

TEST(QueuedEventSinkTest, EngineSubmitDoesNoSynchronousSinkWork) {
    QueuedEventSink<64> sink;
    MatchingEngine engine{&sink};

    // A crossing pair: one accept event + (on the second) one accept +
    // one trade — 3 sink events total, none of which should reach a real
    // sink during submit().
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{10}}});

    // Everything the engine produced is sitting in the ring, untouched by
    // any downstream consumer.
    EXPECT_EQ(sink.pending(), 3u);
    EXPECT_EQ(sink.dropped(), 0u);

    CountingSink real;
    EXPECT_EQ(drain_once(sink, real), 3u);
    EXPECT_EQ(real.calls, 3);
}

// ---------------------------------------------------------------------------
// Parity: driving a real UdpFeedPublisher THROUGH the queue produces the
// same publisher state and the same wire bytes as driving it directly —
// Phase 6's behaviour is unchanged, only *where* it runs moved.
// ---------------------------------------------------------------------------

class QueuedParityTest : public ::testing::Test {
protected:
    static constexpr SymbolId kSymbol{1};

    struct Captured {
        std::vector<std::vector<std::byte>> msgs;
        SendFunction fn() {
            return [this](int, const void* d, std::size_t len,
                          const struct sockaddr*, socklen_t) -> ssize_t {
                std::vector<std::byte> m(len);
                std::memcpy(m.data(), d, len);
                msgs.push_back(std::move(m));
                return static_cast<ssize_t>(len);
            };
        }
    };

    std::unique_ptr<UdpFeedPublisher> make_pub(Captured& cap) {
        std::vector<Subscriber> subs;
        Subscriber s{};
        std::memset(&s.addr, 0, sizeof(s.addr));
        subs.push_back(s);
        return std::make_unique<UdpFeedPublisher>(kSymbol, std::move(subs), -1,
                                                  1000, cap.fn());
    }

    // Same scripted order flow used by the Phase 6 e2e test shape.
    template <typename SinkT>
    void drive(MatchingEngine& engine, SinkT&) {
        engine.submit(NewOrder{
            LimitOrder{OrderId{1}, Side::Sell, Price{101}, Quantity{10}}});
        engine.submit(NewOrder{
            LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{10}}});
        engine.submit(NewOrder{
            LimitOrder{OrderId{3}, Side::Buy, Price{100}, Quantity{20}}});
        engine.submit(NewOrder{
            LimitOrder{OrderId{4}, Side::Buy, Price{101}, Quantity{5}}});
        engine.submit(NewOrder{
            LimitOrder{OrderId{5}, Side::Buy, Price{101}, Quantity{15}}});
        engine.cancel(OrderId{3});
    }
};

TEST_F(QueuedParityTest, QueuedPathMatchesDirectPath) {
    // Direct wiring.
    Captured direct_cap;
    auto direct_pub = make_pub(direct_cap);
    MatchingEngine direct_engine{direct_pub.get()};
    drive(direct_engine, *direct_pub);

    // Queued wiring: engine -> QueuedEventSink -> drain -> real publisher.
    Captured queued_cap;
    auto queued_pub = make_pub(queued_cap);
    QueuedEventSink<1024> sink;
    MatchingEngine queued_engine{&sink};
    drive(queued_engine, sink);
    EXPECT_EQ(sink.dropped(), 0u);
    drain_once(sink, *queued_pub);

    // Same end-state top-of-book.
    auto d = direct_pub->current_top_of_book();
    auto q = queued_pub->current_top_of_book();
    EXPECT_EQ(d.bid_price, q.bid_price);
    EXPECT_EQ(d.bid_qty, q.bid_qty);
    EXPECT_EQ(d.ask_price, q.ask_price);
    EXPECT_EQ(d.ask_qty, q.ask_qty);

    // Same messages, in the same order. Compare structurally rather than
    // byte-for-byte: FeedHeader.timestamp_ns is CLOCK_MONOTONIC at publish
    // time (real on Linux, 0 on non-Linux), so the two runs legitimately
    // differ in that field. Everything that reflects *book state* — the
    // message type/sequence and the payload after the header — must match.
    ASSERT_EQ(direct_cap.msgs.size(), queued_cap.msgs.size());
    constexpr std::size_t kHeaderSize = sizeof(FeedHeader);
    constexpr std::size_t kTsOffset = offsetof(FeedHeader, timestamp_ns);
    for (std::size_t i = 0; i < direct_cap.msgs.size(); ++i) {
        auto a = direct_cap.msgs[i];
        auto b = queued_cap.msgs[i];
        ASSERT_EQ(a.size(), b.size()) << "message " << i << " size";
        ASSERT_GE(a.size(), kHeaderSize);
        // Blank out the timestamp field in both, then require exact equality.
        std::fill_n(a.begin() + kTsOffset, sizeof(uint64_t), std::byte{0});
        std::fill_n(b.begin() + kTsOffset, sizeof(uint64_t), std::byte{0});
        EXPECT_EQ(a, b) << "message " << i << " differs outside the timestamp";
    }
}

// ---------------------------------------------------------------------------
// The drain-thread body: run_feed_publisher drains until stopped, then
// does one final drain so shutdown loses nothing.
// ---------------------------------------------------------------------------

TEST(QueuedEventSinkTest, RunFeedPublisherFinalDrainOnStop) {
    QueuedEventSink<64> sink;
    RecordingSink real;
    std::atomic<bool> stop{false};

    std::thread t([&] { run_feed_publisher(sink, real, stop); });

    for (int i = 0; i < 10; ++i) sink.on_trade(mk_trade(static_cast<uint64_t>(i)));
    // Race the stop against the last pushes; the final drain must still
    // catch anything left.
    sink.on_trade(mk_trade(999));
    stop.store(true, std::memory_order_relaxed);
    t.join();

    EXPECT_EQ(real.trades.size(), 11u);
    EXPECT_EQ(sink.pending(), 0u);
}

}  // namespace
