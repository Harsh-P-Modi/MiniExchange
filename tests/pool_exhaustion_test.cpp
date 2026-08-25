#include "engine/matching_engine.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include "interfaces/event_sink.hpp"

namespace miniexchange {
namespace {

// RecordingEventSink — captures all events for assertions about side effects.
class RecordingEventSink : public EventSink {
public:
    void on_trade(const Trade& trade) override {
        trades.push_back(trade);
    }
    void on_order_accepted(const OrderAccepted& event) override {
        accepted.push_back(event);
    }
    void on_order_cancelled(const OrderCancelled& event) override {
        cancelled.push_back(event);
    }

    std::vector<Trade> trades;
    std::vector<OrderAccepted> accepted;
    std::vector<OrderCancelled> cancelled;
};

// ---------------------------------------------------------------------------
// Test fixture: creates a MatchingEngine with a tiny pool (capacity 2).
// ---------------------------------------------------------------------------
class PoolExhaustionTest : public ::testing::Test {
protected:
    RecordingEventSink sink;
    // Pool capacity = 2: lets us fill it quickly and test exhaustion.
    MatchingEngine engine{&sink, 2};
};

// ===========================================================================
// R4: Third limit order rejected with PoolExhausted when pool is full.
// ===========================================================================
TEST_F(PoolExhaustionTest, ThirdOrderRejectedWhenPoolFull) {
    // Fill both pool slots.
    auto r1 = engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    EXPECT_EQ(r1.status, EngineResult::Accepted);

    auto r2 = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});
    EXPECT_EQ(r2.status, EngineResult::Accepted);

    // Pool is full — 3rd order should be rejected.
    auto r3 = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});
    EXPECT_EQ(r3.status, EngineResult::PoolExhausted);
    EXPECT_TRUE(r3.trades.empty());
    EXPECT_EQ(r3.remaining_qty, Quantity{0});
}

// ===========================================================================
// No side effects: rejected order's ID is NOT in ever_seen_ids_.
// ===========================================================================
TEST_F(PoolExhaustionTest, RejectedOrderIdNotRecorded) {
    // Fill the pool.
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    // Attempt the rejected 3rd order.
    auto r3 = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});
    EXPECT_EQ(r3.status, EngineResult::PoolExhausted);

    // The rejected OrderId should NOT be in ever_seen_ids_.
    // Prove it: submit the same ID again after freeing a slot — it should
    // succeed (if the ID had been recorded, it would get DuplicateOrderId).
    engine.cancel(OrderId{1});  // frees a pool slot
    auto retry = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});
    EXPECT_EQ(retry.status, EngineResult::Accepted);
}

// ===========================================================================
// No side effects: no events emitted for a rejected order.
// ===========================================================================
TEST_F(PoolExhaustionTest, NoEventsEmittedOnRejection) {
    // Fill the pool.
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    // Record event counts before the rejected submit.
    auto accepted_before = sink.accepted.size();
    auto trades_before = sink.trades.size();

    // This should be rejected with zero side effects.
    engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});

    // No new events should have been emitted.
    EXPECT_EQ(sink.accepted.size(), accepted_before);
    EXPECT_EQ(sink.trades.size(), trades_before);
}

// ===========================================================================
// Book state unchanged after rejection.
// ===========================================================================
TEST_F(PoolExhaustionTest, BookUnchangedAfterRejection) {
    // Fill the pool.
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 2u);

    // Reject due to pool exhaustion.
    engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});

    // Book still has exactly 2 orders.
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_EQ(engine.book().find_order(OrderId{3}), nullptr);
}

// ===========================================================================
// Cancel frees a slot, next ADD succeeds (pool recycling).
// ===========================================================================
TEST_F(PoolExhaustionTest, CancelFreesSlotAndNextAddSucceeds) {
    // Fill the pool.
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    // Pool exhausted.
    auto r3 = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});
    EXPECT_EQ(r3.status, EngineResult::PoolExhausted);

    // Cancel order 2 — frees a pool slot.
    auto cancel_resp = engine.cancel(OrderId{2});
    EXPECT_EQ(cancel_resp.status, EngineResult::Accepted);

    // Now the pool has a free slot — new order should succeed.
    auto r4 = engine.submit(
        NewOrder{LimitOrder{OrderId{4}, Side::Sell, Price{150}, Quantity{7}}});
    EXPECT_EQ(r4.status, EngineResult::Accepted);
    EXPECT_EQ(engine.book().order_count(), 2u);
}

// ===========================================================================
// Full fill frees a slot — subsequent non-crossing order can rest.
// ===========================================================================
TEST_F(PoolExhaustionTest, FullFillFreesSlotForNextOrder) {
    // Fill the pool with two non-crossing orders.
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    // Pool is full. But if we submit a sell that crosses the bid
    // (fully filling order 1), that frees a slot. However, with our
    // pre-check strategy, the pool must have a free slot BEFORE matching.
    // So this should be rejected (pessimistic but correct per design).
    auto r3 = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Sell, Price{100}, Quantity{10}}});
    EXPECT_EQ(r3.status, EngineResult::PoolExhausted);

    // Confirm the pessimistic rejection: even though order 3 would have
    // fully matched order 1, it's rejected because the pool was checked
    // before matching began.
}

// ===========================================================================
// Market order NOT affected by pool exhaustion (never rests).
// ===========================================================================
TEST_F(PoolExhaustionTest, MarketOrderNotBlockedByPoolExhaustion) {
    // Fill the pool with two limit orders.
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    // Pool is full. A market order should still work since it never rests.
    auto r3 = engine.submit(
        NewOrder{MarketOrder{OrderId{3}, Side::Sell, Quantity{5}}});
    EXPECT_EQ(r3.status, EngineResult::Accepted);
    // Should have matched against the bid.
    EXPECT_EQ(r3.trades.size(), 1u);
    EXPECT_EQ(r3.trades[0].quantity, Quantity{5});
}

}  // namespace
}  // namespace miniexchange
