#include "engine/matching_engine.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <vector>

#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include "interfaces/event_sink.hpp"

namespace miniexchange {
namespace {

// RecordingEventSink — captures all events for test assertions.
// Implements the full EventSink interface; records each call so tests
// can inspect exact event sequences without depending on EngineResponse
// alone.
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
// Test fixture: creates a MatchingEngine with a RecordingEventSink.
// ---------------------------------------------------------------------------
class MatchingEngineTest : public ::testing::Test {
protected:
    RecordingEventSink sink;
    MatchingEngine engine{&sink};
};

// ===========================================================================
// R1: Valid new limit order rests correctly (no crossing).
// ===========================================================================
TEST_F(MatchingEngineTest, LimitBuyRestsOnEmptyBook) {
    NewOrder order = LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{50}};
    auto response = engine.submit(order);

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{50});

    // Order is resting in the book.
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_NE(engine.book().find_order(OrderId{1}), nullptr);
}

TEST_F(MatchingEngineTest, LimitSellRestsOnEmptyBook) {
    NewOrder order = LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{30}};
    auto response = engine.submit(order);

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{30});

    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_NE(engine.book().find_order(OrderId{2}), nullptr);
}

TEST_F(MatchingEngineTest, NonCrossingLimitOrdersRestOnBothSides) {
    // Buy at 100, Sell at 200 — no cross.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_TRUE(sink.trades.empty());
}

// ===========================================================================
// R2: Duplicate OrderId rejected with DuplicateOrderId.
// ===========================================================================
TEST_F(MatchingEngineTest, DuplicateOrderIdRejected) {
    NewOrder order1 = LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{50}};
    engine.submit(order1);

    // Same ID again — rejection.
    NewOrder order2 = LimitOrder{OrderId{1}, Side::Sell, Price{200}, Quantity{30}};
    auto response = engine.submit(order2);

    EXPECT_EQ(response.status, EngineResult::DuplicateOrderId);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Only the original order rests.
    EXPECT_EQ(engine.book().order_count(), 1u);
}

TEST_F(MatchingEngineTest, DuplicateIdRejectedEvenAfterFullFill) {
    // Place a sell, then a crossing buy that fully fills it.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{10}}});

    // OrderId{1} is fully filled and no longer resting — but reusing it
    // is still rejected: id 1 is <= this client's watermark (now 2), so
    // the per-client monotonic rule (Phase 11 R7) catches it just as the
    // old lifetime-unique set did.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{50}, Quantity{5}}});
    EXPECT_EQ(response.status, EngineResult::DuplicateOrderId);
}

// ===========================================================================
// R3: Zero quantity rejected with InvalidQuantity.
// ===========================================================================
TEST_F(MatchingEngineTest, ZeroQuantityRejected) {
    NewOrder order = LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{0}};
    auto response = engine.submit(order);

    EXPECT_EQ(response.status, EngineResult::InvalidQuantity);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ===========================================================================
// R4: Non-positive price rejected with InvalidPrice.
// ===========================================================================
TEST_F(MatchingEngineTest, ZeroPriceRejected) {
    NewOrder order = LimitOrder{OrderId{1}, Side::Buy, Price{0}, Quantity{50}};
    auto response = engine.submit(order);

    EXPECT_EQ(response.status, EngineResult::InvalidPrice);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(MatchingEngineTest, NegativePriceRejected) {
    NewOrder order = LimitOrder{OrderId{1}, Side::Sell, Price{-1}, Quantity{50}};
    auto response = engine.submit(order);

    EXPECT_EQ(response.status, EngineResult::InvalidPrice);
    EXPECT_TRUE(response.trades.empty());
}

// ===========================================================================
// R16: on_order_accepted emitted exactly once on acceptance.
// R19: No event emitted on rejection.
// ===========================================================================
TEST_F(MatchingEngineTest, OrderAcceptedEventEmittedOnSuccess) {
    NewOrder order = LimitOrder{OrderId{7}, Side::Buy, Price{100}, Quantity{25}};
    engine.submit(order);

    ASSERT_EQ(sink.accepted.size(), 1u);
    EXPECT_EQ(sink.accepted[0].id, OrderId{7});
    EXPECT_EQ(sink.accepted[0].side, Side::Buy);
    EXPECT_EQ(sink.accepted[0].quantity, Quantity{25});
}

TEST_F(MatchingEngineTest, NoEventOnRejection) {
    // All three rejection cases — no events emitted.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{0}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{0}}});

    // First valid, then duplicate.
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Sell, Price{200}, Quantity{10}}});

    // Only the one valid order (OrderId{3}) triggered accepted.
    EXPECT_EQ(sink.accepted.size(), 1u);
    EXPECT_EQ(sink.trades.size(), 0u);
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// ===========================================================================
// Crossing: full fill removes resting order from book.
// ===========================================================================
TEST_F(MatchingEngineTest, CrossingSellFullyFillsRestingBuy) {
    // Resting buy at 100, qty 50.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{50}}});

    // Incoming sell at 100, qty 50 — crosses the buy.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{100}, Quantity{50}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{1});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{2});
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{50});
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Both orders fully filled — nothing resting.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(MatchingEngineTest, CrossingBuyFullyFillsRestingSell) {
    // Resting sell at 100, qty 30.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{30}}});

    // Incoming buy at 100, qty 30 — crosses.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{30}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{30});
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ===========================================================================
// Partial fill: incoming order rests with reduced quantity.
// ===========================================================================
TEST_F(MatchingEngineTest, PartialFillRestsRemainder) {
    // Resting sell at 100, qty 20.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{20}}});

    // Incoming buy at 100, qty 50 — fills 20, rests 30.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{50}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].quantity, Quantity{20});
    EXPECT_EQ(response.remaining_qty, Quantity{30});

    // Resting sell is gone; the buy rests with 30.
    EXPECT_EQ(engine.book().order_count(), 1u);
    Order* resting = engine.book().find_order(OrderId{2});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->quantity, Quantity{30});
    EXPECT_EQ(resting->price, Price{100});
}

TEST_F(MatchingEngineTest, PartialFillOfRestingOrder) {
    // Resting sell at 100, qty 50.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{50}}});

    // Incoming buy at 100, qty 20 — fills 20 of the sell, buy is fully filled.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{20}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].quantity, Quantity{20});
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Resting sell still there with qty 30, buy is fully consumed (not resting).
    EXPECT_EQ(engine.book().order_count(), 1u);
    Order* resting_sell = engine.book().find_order(OrderId{1});
    ASSERT_NE(resting_sell, nullptr);
    EXPECT_EQ(resting_sell->quantity, Quantity{30});
    EXPECT_EQ(engine.book().find_order(OrderId{2}), nullptr);
}

// ===========================================================================
// R6: Trade occurs at resting order's price, not incoming order's price.
// ===========================================================================
TEST_F(MatchingEngineTest, TradeAtRestingOrderPrice) {
    // Resting sell at 95, incoming buy at 100 — trade at 95.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{95}, Quantity{10}}});
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{10}}});

    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].price, Price{95});  // resting order's price
}

// ===========================================================================
// EventSink receives on_trade for each fill.
// ===========================================================================
TEST_F(MatchingEngineTest, EventSinkReceivesOnTradePerFill) {
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{10}}});

    // Buy crosses both levels.
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{101}, Quantity{20}}});

    // EventSink should see 2 trades.
    ASSERT_EQ(sink.trades.size(), 2u);
    EXPECT_EQ(sink.trades[0].price, Price{100});
    EXPECT_EQ(sink.trades[0].quantity, Quantity{10});
    EXPECT_EQ(sink.trades[1].price, Price{101});
    EXPECT_EQ(sink.trades[1].quantity, Quantity{10});
}

// ===========================================================================
// Multiple fills across multiple price levels.
// ===========================================================================
TEST_F(MatchingEngineTest, CrossingMultipleLevels) {
    // Three resting sells at different prices.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{15}}});
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Sell, Price{102}, Quantity{20}}});

    // Buy at 102 for qty 30 — fills level 100 (10) and level 101 (15)
    // and partially fills level 102 (5).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{4}, Side::Buy, Price{102}, Quantity{30}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);

    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[1].price, Price{101});
    EXPECT_EQ(response.trades[1].quantity, Quantity{15});
    EXPECT_EQ(response.trades[2].price, Price{102});
    EXPECT_EQ(response.trades[2].quantity, Quantity{5});

    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Only the partially-filled sell at 102 remains with 15.
    EXPECT_EQ(engine.book().order_count(), 1u);
    Order* remaining_sell = engine.book().find_order(OrderId{3});
    ASSERT_NE(remaining_sell, nullptr);
    EXPECT_EQ(remaining_sell->quantity, Quantity{15});
}

// ===========================================================================
// Non-crossing: sell price above best bid does not match.
// ===========================================================================
TEST_F(MatchingEngineTest, SellAboveBestBidDoesNotCross) {
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});

    // Sell at 101 — higher than best bid (100), no cross.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{10}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(engine.book().order_count(), 2u);
}

TEST_F(MatchingEngineTest, BuyBelowBestAskDoesNotCross) {
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});

    // Buy at 99 — lower than best ask (100), no cross.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{99}, Quantity{10}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(engine.book().order_count(), 2u);
}

// ===========================================================================
// Sequence and TradeSequence monotonically increase.
// ===========================================================================
TEST_F(MatchingEngineTest, SequenceCounterIncrements) {
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{10}}});

    Order* o1 = engine.book().find_order(OrderId{1});
    Order* o2 = engine.book().find_order(OrderId{2});
    ASSERT_NE(o1, nullptr);
    ASSERT_NE(o2, nullptr);

    // Second order has a later sequence.
    EXPECT_TRUE(o1->sequence < o2->sequence);
}

TEST_F(MatchingEngineTest, TradeSequenceCounterIncrements) {
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{10}}});

    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{101}, Quantity{20}}});

    ASSERT_EQ(response.trades.size(), 2u);
    EXPECT_TRUE(response.trades[0].trade_sequence < response.trades[1].trade_sequence);
}

// ===========================================================================
// NullEventSink: engine works without a wired sink.
// ===========================================================================
TEST(MatchingEngineNullSinkTest, WorksWithDefaultNullSink) {
    MatchingEngine engine;  // uses NullEventSink::instance()
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_EQ(engine.book().order_count(), 1u);
}

// ===========================================================================
// FIFO within a level during matching.
// ===========================================================================
TEST_F(MatchingEngineTest, FIFOMatchingWithinLevel) {
    // Two sells at the same price — first in should fill first.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{100}, Quantity{10}}});

    // Buy that partially fills the level — should only match order 1.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{100}, Quantity{10}}});

    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{1});  // first in

    // Order 1 is gone, order 2 still resting.
    EXPECT_EQ(engine.book().find_order(OrderId{1}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{2}), nullptr);
}

// ===========================================================================
// Task 10 — Additional FIFO / price-time priority tests.
// Validates R5 (price-time priority), R7 (fully consumed removed), R20
// (EventSink synchronous, same order as EngineResponse).
// ===========================================================================

// Three orders at same price; incoming fills qty for exactly 2.5 of them.
// First two fully consumed, third partially filled.
TEST_F(MatchingEngineTest, FIFOPartialFillThirdOrderAtSamePrice) {
    // Three resting sells at price 100, qty 10 each.
    engine.submit(NewOrder{LimitOrder{OrderId{10}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{11}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{12}, Side::Sell, Price{100}, Quantity{10}}});

    // Incoming buy at 100 for qty 25 — fills 10+10+5 (partial on third).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{13}, Side::Buy, Price{100}, Quantity{25}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);

    // First trade: against order 10 (first inserted), full qty 10.
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{10});
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{13});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[0].price, Price{100});

    // Second trade: against order 11, full qty 10.
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{11});
    EXPECT_EQ(response.trades[1].buy_order_id, OrderId{13});
    EXPECT_EQ(response.trades[1].quantity, Quantity{10});
    EXPECT_EQ(response.trades[1].price, Price{100});

    // Third trade: against order 12, partial qty 5.
    EXPECT_EQ(response.trades[2].sell_order_id, OrderId{12});
    EXPECT_EQ(response.trades[2].buy_order_id, OrderId{13});
    EXPECT_EQ(response.trades[2].quantity, Quantity{5});
    EXPECT_EQ(response.trades[2].price, Price{100});

    // Incoming buy fully filled (remaining 0).
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Orders 10, 11 fully consumed — removed from book.
    EXPECT_EQ(engine.book().find_order(OrderId{10}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{11}), nullptr);

    // Order 12 still resting with remaining qty 5.
    Order* remaining = engine.book().find_order(OrderId{12});
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->quantity, Quantity{5});

    // Total resting: only order 12.
    EXPECT_EQ(engine.book().order_count(), 1u);
}

// Trade sequence numbers within a single submission are strictly monotonically
// increasing.
TEST_F(MatchingEngineTest, TradeSequenceStrictlyIncreasingWithinSubmission) {
    // Three resting sells at different prices.
    engine.submit(NewOrder{LimitOrder{OrderId{20}, Side::Sell, Price{100}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{21}, Side::Sell, Price{101}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{22}, Side::Sell, Price{102}, Quantity{5}}});

    // Buy sweeps all three.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{23}, Side::Buy, Price{102}, Quantity{15}}});

    ASSERT_EQ(response.trades.size(), 3u);
    // Strict monotonic increase: each trade_sequence > previous.
    EXPECT_TRUE(response.trades[0].trade_sequence < response.trades[1].trade_sequence);
    EXPECT_TRUE(response.trades[1].trade_sequence < response.trades[2].trade_sequence);
}

// EventSink on_trade calls arrive in the same order as EngineResponse.trades
// entries — verifying R20 (synchronous, before EngineResponse returns).
TEST_F(MatchingEngineTest, EventSinkOnTradeOrderMatchesEngineResponse) {
    // Two resting sells at same price.
    engine.submit(NewOrder{LimitOrder{OrderId{30}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{31}, Side::Sell, Price{100}, Quantity{10}}});

    // Clear the sink to ignore the accepted events from setup.
    sink.trades.clear();

    // Buy sweeps both.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{32}, Side::Buy, Price{100}, Quantity{20}}});

    ASSERT_EQ(response.trades.size(), 2u);
    ASSERT_EQ(sink.trades.size(), 2u);

    // Each EventSink trade matches the corresponding EngineResponse trade.
    for (std::size_t i = 0; i < response.trades.size(); ++i) {
        EXPECT_EQ(sink.trades[i].trade_sequence, response.trades[i].trade_sequence);
        EXPECT_EQ(sink.trades[i].buy_order_id, response.trades[i].buy_order_id);
        EXPECT_EQ(sink.trades[i].sell_order_id, response.trades[i].sell_order_id);
        EXPECT_EQ(sink.trades[i].price, response.trades[i].price);
        EXPECT_EQ(sink.trades[i].quantity, response.trades[i].quantity);
    }
}

// Multiple levels swept in correct price order: an incoming buy at 102
// should fill at 100 first (best ask), then 101, then 102.
TEST_F(MatchingEngineTest, PriceTimePrioritySweepsLevelsInOrder) {
    // Insert sells in non-price order to verify the book sorts them.
    engine.submit(NewOrder{LimitOrder{OrderId{40}, Side::Sell, Price{102}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{41}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{42}, Side::Sell, Price{101}, Quantity{10}}});

    // Clear sink to focus on the sweep.
    sink.trades.clear();

    // Buy at 102 for qty 30 — should fill at 100, 101, 102 in that order.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{43}, Side::Buy, Price{102}, Quantity{30}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);

    // Price priority: best ask (100) first, then 101, then 102.
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{41});
    EXPECT_EQ(response.trades[1].price, Price{101});
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{42});
    EXPECT_EQ(response.trades[2].price, Price{102});
    EXPECT_EQ(response.trades[2].sell_order_id, OrderId{40});

    // EventSink also receives in same order.
    ASSERT_EQ(sink.trades.size(), 3u);
    EXPECT_EQ(sink.trades[0].price, Price{100});
    EXPECT_EQ(sink.trades[1].price, Price{101});
    EXPECT_EQ(sink.trades[2].price, Price{102});

    // Book is empty after full sweep.
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// Same-level FIFO with time priority: multiple orders at same price,
// verify earlier-inserted (lower Sequence) fills before later.
TEST_F(MatchingEngineTest, FIFOWithinLevelRespectedForMultipleOrders) {
    // Five bids at price 50 — verify matching order respects insertion.
    engine.submit(NewOrder{LimitOrder{OrderId{50}, Side::Buy, Price{50}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{51}, Side::Buy, Price{50}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{52}, Side::Buy, Price{50}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{53}, Side::Buy, Price{50}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{54}, Side::Buy, Price{50}, Quantity{5}}});

    // Sell at 50 for qty 12 — fills orders 50 (5), 51 (5), 52 (2 partial).
    sink.trades.clear();
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{55}, Side::Sell, Price{50}, Quantity{12}}});

    ASSERT_EQ(response.trades.size(), 3u);

    // FIFO: order 50 first, then 51, then partial of 52.
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{50});
    EXPECT_EQ(response.trades[0].quantity, Quantity{5});
    EXPECT_EQ(response.trades[1].buy_order_id, OrderId{51});
    EXPECT_EQ(response.trades[1].quantity, Quantity{5});
    EXPECT_EQ(response.trades[2].buy_order_id, OrderId{52});
    EXPECT_EQ(response.trades[2].quantity, Quantity{2});

    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Orders 50, 51 removed; 52 partially filled (3 left); 53, 54 untouched.
    EXPECT_EQ(engine.book().find_order(OrderId{50}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{51}), nullptr);
    Order* partial = engine.book().find_order(OrderId{52});
    ASSERT_NE(partial, nullptr);
    EXPECT_EQ(partial->quantity, Quantity{3});
    EXPECT_NE(engine.book().find_order(OrderId{53}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{54}), nullptr);

    // EventSink saw same order.
    ASSERT_EQ(sink.trades.size(), 3u);
    EXPECT_EQ(sink.trades[0].buy_order_id, OrderId{50});
    EXPECT_EQ(sink.trades[1].buy_order_id, OrderId{51});
    EXPECT_EQ(sink.trades[2].buy_order_id, OrderId{52});
}

// Self-crossing matches normally (R14) — included here as a sweep test since
// it exercises the matching loop with "same submitter" conceptually.
TEST_F(MatchingEngineTest, SelfCrossingMatchesNormally) {
    // Place a sell, then submit a buy that crosses it — same "user" conceptually.
    engine.submit(NewOrder{LimitOrder{OrderId{60}, Side::Sell, Price{100}, Quantity{10}}});
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{61}, Side::Buy, Price{100}, Quantity{10}}});

    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{61});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{60});
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ===========================================================================
// Task 11 — Market-order submission (R9, R10, R11).
// ===========================================================================

// Market buy fills available sell liquidity fully.
TEST_F(MatchingEngineTest, MarketBuyFullyFillsAvailableSells) {
    // Resting sells at two levels.
    engine.submit(NewOrder{LimitOrder{OrderId{100}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{101}, Side::Sell, Price{101}, Quantity{10}}});

    // Market buy for qty 20 — sweeps all sell liquidity.
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{102}, Side::Buy, Quantity{20}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 2u);
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{102});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{100});
    EXPECT_EQ(response.trades[1].price, Price{101});
    EXPECT_EQ(response.trades[1].quantity, Quantity{10});
    EXPECT_EQ(response.trades[1].buy_order_id, OrderId{102});
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{101});
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Book empty on the sell side.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// Market sell fills available buy liquidity fully.
TEST_F(MatchingEngineTest, MarketSellFullyFillsAvailableBuys) {
    engine.submit(NewOrder{LimitOrder{OrderId{110}, Side::Buy, Price{50}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{111}, Side::Buy, Price{51}, Quantity{10}}});

    // Market sell for qty 20 — sweeps all buy liquidity (best bid first: 51, then 50).
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{112}, Side::Sell, Quantity{20}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 2u);
    // Best bid is 51, matched first.
    EXPECT_EQ(response.trades[0].price, Price{51});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{112});
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{111});
    EXPECT_EQ(response.trades[1].price, Price{50});
    EXPECT_EQ(response.trades[1].quantity, Quantity{10});
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{112});
    EXPECT_EQ(response.trades[1].buy_order_id, OrderId{110});
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    EXPECT_EQ(engine.book().order_count(), 0u);
}

// Market order on empty opposite side: accepted, 0 trades, remaining = full qty.
TEST_F(MatchingEngineTest, MarketBuyOnEmptyBookNoFills) {
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{120}, Side::Buy, Quantity{50}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{50});

    // Market order does NOT rest.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(MatchingEngineTest, MarketSellOnEmptyBookNoFills) {
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{121}, Side::Sell, Quantity{30}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{30});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// Market order partially fills — remainder NOT resting on book (R10).
TEST_F(MatchingEngineTest, MarketBuyPartialFillDoesNotRest) {
    // Only 10 available to sell.
    engine.submit(NewOrder{LimitOrder{OrderId{130}, Side::Sell, Price{100}, Quantity{10}}});

    // Market buy for 25 — fills 10, remaining 15 discarded (not resting).
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{131}, Side::Buy, Quantity{25}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.remaining_qty, Quantity{15});

    // The resting sell is consumed. Market order did NOT rest.
    EXPECT_EQ(engine.book().order_count(), 0u);
    EXPECT_EQ(engine.book().find_order(OrderId{131}), nullptr);
}

TEST_F(MatchingEngineTest, MarketSellPartialFillDoesNotRest) {
    engine.submit(NewOrder{LimitOrder{OrderId{140}, Side::Buy, Price{50}, Quantity{8}}});

    // Market sell for 20 — fills 8, remaining 12 discarded.
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{141}, Side::Sell, Quantity{20}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].quantity, Quantity{8});
    EXPECT_EQ(response.remaining_qty, Quantity{12});

    EXPECT_EQ(engine.book().order_count(), 0u);
    EXPECT_EQ(engine.book().find_order(OrderId{141}), nullptr);
}

// Market order with duplicate ID → DuplicateOrderId.
TEST_F(MatchingEngineTest, MarketOrderDuplicateIdRejected) {
    // First, accept a limit order with ID 150.
    engine.submit(NewOrder{LimitOrder{OrderId{150}, Side::Buy, Price{100}, Quantity{10}}});

    // Market order reusing same ID — rejected.
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{150}, Side::Sell, Quantity{5}}});

    EXPECT_EQ(response.status, EngineResult::DuplicateOrderId);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});
}

// Market order with zero quantity → InvalidQuantity.
TEST_F(MatchingEngineTest, MarketOrderZeroQuantityRejected) {
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{160}, Side::Buy, Quantity{0}}});

    EXPECT_EQ(response.status, EngineResult::InvalidQuantity);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});
}

// EventSink receives on_order_accepted + on_trade calls for market orders.
TEST_F(MatchingEngineTest, MarketOrderEmitsAcceptedAndTradeEvents) {
    engine.submit(NewOrder{LimitOrder{OrderId{170}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{171}, Side::Sell, Price{101}, Quantity{10}}});

    // Clear to focus on market order events.
    sink.accepted.clear();
    sink.trades.clear();

    // Market buy sweeps both levels.
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{172}, Side::Buy, Quantity{20}}});

    // on_order_accepted emitted once for the market order.
    ASSERT_EQ(sink.accepted.size(), 1u);
    EXPECT_EQ(sink.accepted[0].id, OrderId{172});
    EXPECT_EQ(sink.accepted[0].side, Side::Buy);
    EXPECT_EQ(sink.accepted[0].quantity, Quantity{20});

    // on_trade emitted per fill (two fills).
    ASSERT_EQ(sink.trades.size(), 2u);
    EXPECT_EQ(sink.trades[0].price, Price{100});
    EXPECT_EQ(sink.trades[0].quantity, Quantity{10});
    EXPECT_EQ(sink.trades[1].price, Price{101});
    EXPECT_EQ(sink.trades[1].quantity, Quantity{10});
}

// Market order rejected events: no EventSink calls on rejection (R19).
TEST_F(MatchingEngineTest, MarketOrderRejectionNoEvents) {
    // Accept one order to have a duplicate ID available.
    engine.submit(NewOrder{LimitOrder{OrderId{180}, Side::Buy, Price{100}, Quantity{10}}});
    sink.accepted.clear();

    // Zero qty rejection.
    engine.submit(NewOrder{MarketOrder{OrderId{181}, Side::Buy, Quantity{0}}});
    EXPECT_EQ(sink.accepted.size(), 0u);

    // Duplicate ID rejection.
    engine.submit(NewOrder{MarketOrder{OrderId{180}, Side::Sell, Quantity{5}}});
    EXPECT_EQ(sink.accepted.size(), 0u);
    EXPECT_EQ(sink.trades.size(), 0u);
}

// Market order that fully sweeps all opposite liquidity: remaining = 0,
// book empty on that side.
TEST_F(MatchingEngineTest, MarketOrderFullSweepEmptiesOppositeSide) {
    // Place three sell levels.
    engine.submit(NewOrder{LimitOrder{OrderId{190}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{191}, Side::Sell, Price{200}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{192}, Side::Sell, Price{300}, Quantity{10}}});

    // Market buy with exact total quantity (30).
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{193}, Side::Buy, Quantity{30}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// Confirm MarketOrder has no price field — compile-time structural guarantee.
// (R11: enforced by type system, not runtime check.)
// This is already verified in test_order_types.cpp via a static_assert
// using a concept-based approach. Here we simply confirm the market
// order type is exercised through submit()'s std::visit dispatch by
// verifying it reaches submit_market (which a successful market order
// test above proves — the std::visit path is working).
TEST_F(MatchingEngineTest, MarketOrderDispatchesViaVariant) {
    // Constructing a MarketOrder — no price field to set, confirming
    // the variant path is exercised.
    NewOrder order = MarketOrder{OrderId{199}, Side::Buy, Quantity{5}};
    auto response = engine.submit(order);
    // If we get here, std::visit dispatched to submit_market correctly.
    EXPECT_EQ(response.status, EngineResult::Accepted);
}

// ===========================================================================
// Task 12 — Cancel logic (R12, R13, R18, R19).
// ===========================================================================

// Cancel a resting order: removed from book, on_order_cancelled fires
// exactly once, Accepted returned with correct remaining_qty.
TEST_F(MatchingEngineTest, CancelRestingOrderSucceeds) {
    engine.submit(NewOrder{LimitOrder{OrderId{200}, Side::Buy, Price{100}, Quantity{42}}});
    ASSERT_EQ(engine.book().order_count(), 1u);

    // Clear events from the submit.
    sink.cancelled.clear();
    sink.accepted.clear();

    auto response = engine.cancel(OrderId{200});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{42});

    // Order removed from book.
    EXPECT_EQ(engine.book().order_count(), 0u);
    EXPECT_EQ(engine.book().find_order(OrderId{200}), nullptr);

    // EventSink received on_order_cancelled exactly once.
    ASSERT_EQ(sink.cancelled.size(), 1u);
    EXPECT_EQ(sink.cancelled[0].id, OrderId{200});
    EXPECT_EQ(sink.cancelled[0].remaining_qty, Quantity{42});
}

// Cancel a partially-filled resting order: remaining_qty reflects
// the reduced quantity at cancellation time (not original quantity).
TEST_F(MatchingEngineTest, CancelPartiallyFilledOrderReportsCorrectRemaining) {
    // Sell 50 at 100.
    engine.submit(NewOrder{LimitOrder{OrderId{210}, Side::Sell, Price{100}, Quantity{50}}});
    // Buy 20 at 100 — partially fills the sell (30 remains resting).
    engine.submit(NewOrder{LimitOrder{OrderId{211}, Side::Buy, Price{100}, Quantity{20}}});

    ASSERT_EQ(engine.book().order_count(), 1u);
    Order* resting = engine.book().find_order(OrderId{210});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->quantity, Quantity{30});

    sink.cancelled.clear();

    auto response = engine.cancel(OrderId{210});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_EQ(response.remaining_qty, Quantity{30});  // remaining at cancel time
    EXPECT_EQ(engine.book().order_count(), 0u);

    ASSERT_EQ(sink.cancelled.size(), 1u);
    EXPECT_EQ(sink.cancelled[0].remaining_qty, Quantity{30});
}

// Cancel an unknown/never-existed ID: UnknownOrderId, no event fired.
TEST_F(MatchingEngineTest, CancelUnknownIdReturnsUnknownOrderId) {
    auto response = engine.cancel(OrderId{999});

    EXPECT_EQ(response.status, EngineResult::UnknownOrderId);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // No events emitted (R19).
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// Cancel an already-fully-filled order's ID: UnknownOrderId (R13).
// This distinguishes "resting" from "seen before" — the id is at/below
// the client's monotonic watermark but NOT in the book, so cancel
// correctly rejects it (cancel never consults the watermark).
TEST_F(MatchingEngineTest, CancelAlreadyFilledOrderReturnsUnknownOrderId) {
    // Sell 10 at 100, then buy 10 at 100 — sell fully filled, removed from book.
    engine.submit(NewOrder{LimitOrder{OrderId{220}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{221}, Side::Buy, Price{100}, Quantity{10}}});

    // OrderId{220} is fully filled — not resting.
    EXPECT_EQ(engine.book().find_order(OrderId{220}), nullptr);

    sink.cancelled.clear();

    auto response = engine.cancel(OrderId{220});

    EXPECT_EQ(response.status, EngineResult::UnknownOrderId);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// Double cancel: second cancel returns UnknownOrderId.
TEST_F(MatchingEngineTest, DoubleCancelReturnsUnknownOrderId) {
    engine.submit(NewOrder{LimitOrder{OrderId{230}, Side::Sell, Price{200}, Quantity{15}}});

    // First cancel — succeeds.
    auto response1 = engine.cancel(OrderId{230});
    EXPECT_EQ(response1.status, EngineResult::Accepted);

    // Second cancel — order no longer resting.
    sink.cancelled.clear();

    auto response2 = engine.cancel(OrderId{230});
    EXPECT_EQ(response2.status, EngineResult::UnknownOrderId);
    EXPECT_EQ(response2.remaining_qty, Quantity{0});
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// After cancel, order_count() decrements and find_order returns nullptr.
TEST_F(MatchingEngineTest, CancelDecrementsOrderCountAndRemovesFromLookup) {
    engine.submit(NewOrder{LimitOrder{OrderId{240}, Side::Buy, Price{50}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{241}, Side::Buy, Price{51}, Quantity{10}}});
    EXPECT_EQ(engine.book().order_count(), 2u);

    engine.cancel(OrderId{240});
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().find_order(OrderId{240}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{241}), nullptr);

    engine.cancel(OrderId{241});
    EXPECT_EQ(engine.book().order_count(), 0u);
    EXPECT_EQ(engine.book().find_order(OrderId{241}), nullptr);
}

// R19: rejection (UnknownOrderId from cancel) does NOT trigger any event.
TEST_F(MatchingEngineTest, CancelRejectionNoEvents) {
    // Baseline: no events emitted.
    EXPECT_EQ(sink.accepted.size(), 0u);
    EXPECT_EQ(sink.trades.size(), 0u);
    EXPECT_EQ(sink.cancelled.size(), 0u);

    engine.cancel(OrderId{777});

    // Still no events.
    EXPECT_EQ(sink.accepted.size(), 0u);
    EXPECT_EQ(sink.trades.size(), 0u);
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// ===========================================================================
// Phase 8 / T2 — ClientId owner threads onto the resting Order.
// A limit order submitted with an owner must carry that owner onto the
// resting Order in the book, so STP (T5) can read it later.
// ===========================================================================
TEST_F(MatchingEngineTest, RestingOrderRetainsSubmitterOwner) {
    LimitOrder order{OrderId{300}, Side::Buy, Price{100}, Quantity{10}};
    order.owner = ClientId{42};
    engine.submit(NewOrder{order});

    Order* resting = engine.book().find_order(OrderId{300});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->owner, ClientId{42});
}

// A partially-filled incoming limit order that rests keeps its owner on the
// resting remainder.
TEST_F(MatchingEngineTest, PartiallyFilledRemainderRetainsOwner) {
    // Resting sell (owner 7), qty 20.
    LimitOrder sell{OrderId{310}, Side::Sell, Price{100}, Quantity{20}};
    sell.owner = ClientId{7};
    engine.submit(NewOrder{sell});

    // Incoming buy (owner 8), qty 50 — fills 20, rests 30.
    LimitOrder buy{OrderId{311}, Side::Buy, Price{100}, Quantity{50}};
    buy.owner = ClientId{8};
    engine.submit(NewOrder{buy});

    Order* resting = engine.book().find_order(OrderId{311});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->quantity, Quantity{30});
    EXPECT_EQ(resting->owner, ClientId{8});
}

// Default owner (ClientId{0}) when the submitter didn't set one — confirms
// the defaulted trailing field behaves as documented.
TEST_F(MatchingEngineTest, RestingOrderDefaultsOwnerToZeroWhenUnset) {
    engine.submit(NewOrder{LimitOrder{OrderId{320}, Side::Buy, Price{100}, Quantity{10}}});

    Order* resting = engine.book().find_order(OrderId{320});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->owner, ClientId{0});
}

// ===========================================================================
// Phase 8 / T5 — Self-trade prevention (R5), in the engine match loop.
// ===========================================================================

// Helper to build a limit order with an explicit owner.
static LimitOrder owned_limit(uint64_t id, Side side, int64_t price,
                              uint64_t qty, uint64_t owner) {
    LimitOrder o{OrderId{id}, side, Price{price}, Quantity{qty}};
    o.owner = ClientId{owner};
    return o;
}

static MarketOrder owned_market(uint64_t id, Side side, uint64_t qty,
                                uint64_t owner) {
    MarketOrder o{OrderId{id}, side, Quantity{qty}};
    o.owner = ClientId{owner};
    return o;
}

// --- RejectIncoming (default policy) ---

class StpRejectTest : public ::testing::Test {
protected:
    RecordingEventSink sink;
    MatchingEngine engine{&sink, 1'000'000,
                          StpConfig{true, StpPolicy::RejectIncoming}};
};

// Same-owner cross is rejected with SelfTradePrevented, and — critically —
// consumes no OrderId, emits no events, and leaves the resting order intact.
TEST_F(StpRejectTest, SameOwnerCrossRejectedWithNoSideEffects) {
    // Client 1 rests a sell at 100.
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    sink.accepted.clear();
    sink.trades.clear();
    sink.cancelled.clear();

    // Client 1 submits a buy that would cross its own sell.
    auto resp =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 100, 10, /*owner=*/1)});

    EXPECT_EQ(resp.status, EngineResult::SelfTradePrevented);
    EXPECT_TRUE(resp.trades.empty());
    EXPECT_EQ(resp.remaining_qty, Quantity{0});

    // Resting sell untouched; incoming order did NOT rest.
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_NE(engine.book().find_order(OrderId{1}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{2}), nullptr);

    // No events at all from the rejected order.
    EXPECT_EQ(sink.accepted.size(), 0u);
    EXPECT_EQ(sink.trades.size(), 0u);
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// The rejected OrderId is NOT consumed — it can be reused successfully.
TEST_F(StpRejectTest, RejectedOrderIdNotConsumed) {
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});

    // Rejected self-cross using OrderId 2.
    auto rejected =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 100, 10, /*owner=*/1)});
    ASSERT_EQ(rejected.status, EngineResult::SelfTradePrevented);

    // Reuse OrderId 2 from a DIFFERENT client, non-crossing — must succeed.
    auto reused =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 90, 10, /*owner=*/2)});
    EXPECT_EQ(reused.status, EngineResult::Accepted);
}

// Different-owner cross proceeds normally (STP only guards same owner).
TEST_F(StpRejectTest, DifferentOwnerCrossProceeds) {
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    auto resp =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 100, 10, /*owner=*/2)});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    ASSERT_EQ(resp.trades.size(), 1u);
    EXPECT_EQ(resp.trades[0].quantity, Quantity{10});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// A same-owner order that does NOT cross (price doesn't reach) is accepted
// and rests — STP only fires on an actual would-cross.
TEST_F(StpRejectTest, SameOwnerNonCrossingRestsNormally) {
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    // Buy at 90 — below the ask, does not cross.
    auto resp =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 90, 10, /*owner=*/1)});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_TRUE(resp.trades.empty());
    EXPECT_EQ(engine.book().order_count(), 2u);
}

// Market order self-cross is rejected too (nullopt limit scans all levels).
TEST_F(StpRejectTest, SameOwnerMarketCrossRejected) {
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/7)});
    auto resp = engine.submit(NewOrder{owned_market(2, Side::Buy, 5, /*owner=*/7)});

    EXPECT_EQ(resp.status, EngineResult::SelfTradePrevented);
    EXPECT_EQ(engine.book().order_count(), 1u);  // resting sell intact
}

// STP does not fire against a same-owner order sitting BEHIND a
// different-owner order the incoming would fill first — but our pre-scan is
// conservative: it rejects if ANY crossable same-owner order exists. Verify
// that documented behavior (reject even if a different-owner order is in front).
TEST_F(StpRejectTest, RejectsWhenSameOwnerRestsBehindOtherOwnerAtCrossablePrice) {
    // Two sells at 100: client 2 first (FIFO front), client 1 behind.
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/2)});
    engine.submit(NewOrder{owned_limit(2, Side::Sell, 100, 10, /*owner=*/1)});

    // Client 1 buys enough to reach its own resting order at the same level.
    auto resp =
        engine.submit(NewOrder{owned_limit(3, Side::Buy, 100, 20, /*owner=*/1)});

    EXPECT_EQ(resp.status, EngineResult::SelfTradePrevented);
    // Nothing traded, both sells intact.
    EXPECT_EQ(engine.book().order_count(), 2u);
}

// --- CancelResting policy ---

class StpCancelTest : public ::testing::Test {
protected:
    RecordingEventSink sink;
    MatchingEngine engine{&sink, 1'000'000,
                          StpConfig{true, StpPolicy::CancelResting}};
};

// Same-owner resting order is cancelled (not traded), incoming proceeds.
TEST_F(StpCancelTest, SameOwnerRestingCancelledIncomingProceeds) {
    // Client 1 rests a sell at 100, qty 10.
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    sink.cancelled.clear();
    sink.trades.clear();

    // Client 1 submits a buy at 100, qty 10 — would self-cross.
    // Policy: cancel the resting sell, let the buy proceed. With no other
    // liquidity, the buy then rests.
    auto resp =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 100, 10, /*owner=*/1)});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_TRUE(resp.trades.empty());  // no self-trade occurred

    // Resting sell was cancelled (event emitted), incoming buy now rests.
    ASSERT_EQ(sink.cancelled.size(), 1u);
    EXPECT_EQ(sink.cancelled[0].id, OrderId{1});
    EXPECT_EQ(engine.book().find_order(OrderId{1}), nullptr);  // gone
    Order* buy = engine.book().find_order(OrderId{2});
    ASSERT_NE(buy, nullptr);
    EXPECT_EQ(buy->quantity, Quantity{10});  // rested unfilled
}

// CancelResting pulls the same-owner order but still trades against a
// different-owner order at the same level.
TEST_F(StpCancelTest, CancelsOwnRestingButTradesWithOthers) {
    // Sell at 100 from client 1 (front), then sell at 100 from client 2.
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    engine.submit(NewOrder{owned_limit(2, Side::Sell, 100, 10, /*owner=*/2)});
    sink.cancelled.clear();
    sink.trades.clear();

    // Client 1 buys 10 at 100: its own resting sell (front) is cancelled,
    // then it trades against client 2's sell.
    auto resp =
        engine.submit(NewOrder{owned_limit(3, Side::Buy, 100, 10, /*owner=*/1)});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    // Cancelled own order 1.
    ASSERT_EQ(sink.cancelled.size(), 1u);
    EXPECT_EQ(sink.cancelled[0].id, OrderId{1});
    // Traded against order 2.
    ASSERT_EQ(resp.trades.size(), 1u);
    EXPECT_EQ(resp.trades[0].sell_order_id, OrderId{2});
    EXPECT_EQ(resp.trades[0].quantity, Quantity{10});
    // Book empty: order 1 cancelled, order 2 filled, buy fully filled.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// --- STP disabled (default engine): self-cross trades normally ---

TEST_F(MatchingEngineTest, StpDisabledSelfCrossTradesNormally) {
    // Default `engine` fixture has STP disabled.
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    auto resp =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 100, 10, /*owner=*/1)});

    // No STP: the self-cross trades.
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    ASSERT_EQ(resp.trades.size(), 1u);
    EXPECT_EQ(resp.trades[0].quantity, Quantity{10});
}

// ===========================================================================
// Phase 11 T2 (R2) — every EngineResponse carries the OrderId it answers,
// on the accept path and on every rejection path, for submit and cancel.
// ===========================================================================

TEST_F(MatchingEngineTest, R2_AcceptPathCarriesOrderId) {
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{55}, Side::Buy, Price{100}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.order_id, OrderId{55});
}

TEST_F(MatchingEngineTest, R2_MarketAcceptPathCarriesOrderId) {
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    auto resp = engine.submit(
        NewOrder{MarketOrder{OrderId{56}, Side::Buy, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.order_id, OrderId{56});
}

TEST_F(MatchingEngineTest, R2_DuplicateOrderIdRejectionCarriesOrderId) {
    engine.submit(
        NewOrder{LimitOrder{OrderId{7}, Side::Buy, Price{100}, Quantity{10}}});
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{7}, Side::Buy, Price{101}, Quantity{5}}});
    EXPECT_EQ(resp.status, EngineResult::DuplicateOrderId);
    EXPECT_EQ(resp.order_id, OrderId{7});
}

TEST_F(MatchingEngineTest, R2_InvalidQuantityRejectionCarriesOrderId) {
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{8}, Side::Buy, Price{100}, Quantity{0}}});
    EXPECT_EQ(resp.status, EngineResult::InvalidQuantity);
    EXPECT_EQ(resp.order_id, OrderId{8});
}

TEST_F(MatchingEngineTest, R2_InvalidPriceRejectionCarriesOrderId) {
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{9}, Side::Buy, Price{0}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::InvalidPrice);
    EXPECT_EQ(resp.order_id, OrderId{9});
}

TEST_F(MatchingEngineTest, R2_UnknownCancelRejectionCarriesOrderId) {
    auto resp = engine.cancel(OrderId{404});
    EXPECT_EQ(resp.status, EngineResult::UnknownOrderId);
    EXPECT_EQ(resp.order_id, OrderId{404});
}

TEST_F(MatchingEngineTest, R2_CancelAcceptCarriesOrderId) {
    engine.submit(
        NewOrder{LimitOrder{OrderId{11}, Side::Buy, Price{100}, Quantity{10}}});
    auto resp = engine.cancel(OrderId{11});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.order_id, OrderId{11});
}

TEST_F(StpRejectTest, R2_SelfTradePreventedRejectionCarriesOrderId) {
    engine.submit(NewOrder{owned_limit(1, Side::Sell, 100, 10, /*owner=*/1)});
    auto resp =
        engine.submit(NewOrder{owned_limit(2, Side::Buy, 100, 10, /*owner=*/1)});
    EXPECT_EQ(resp.status, EngineResult::SelfTradePrevented);
    EXPECT_EQ(resp.order_id, OrderId{2});
}

TEST(MatchingEnginePoolExhaustionR2, PoolExhaustedRejectionCarriesOrderId) {
    RecordingEventSink sink;
    MatchingEngine engine{&sink, /*pool_capacity=*/1};
    engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{99}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::PoolExhausted);
    EXPECT_EQ(resp.order_id, OrderId{2});
}

// The DoD's "pipelined pair" case: two orders submitted back-to-back, each
// response correlated to its own originating order by OrderId.
TEST_F(MatchingEngineTest, R2_PipelinedPairCorrelatedByOrderId) {
    auto r1 = engine.submit(
        NewOrder{LimitOrder{OrderId{100}, Side::Buy, Price{100}, Quantity{10}}});
    auto r2 = engine.submit(
        NewOrder{LimitOrder{OrderId{101}, Side::Sell, Price{100}, Quantity{4}}});
    EXPECT_EQ(r1.order_id, OrderId{100});
    EXPECT_EQ(r2.order_id, OrderId{101});
    // r2 crossed r1 — its trade references both, but the response id is r2's.
    ASSERT_EQ(r2.trades.size(), 1u);
    EXPECT_EQ(r2.order_id, OrderId{101});
}

// ===========================================================================
// Phase 11 T7 (R7) — per-client monotonic OrderId watermark replaces the
// unbounded global ever_seen_ids_ set. This is a deliberate semantic
// narrowing (requirements.md §7): uniqueness is now per-client and
// monotonic, not global-lifetime. The wire contract (DuplicateOrderId) is
// unchanged.
// ===========================================================================

// NEW BEHAVIOUR: the same numeric OrderId from two *different* clients is
// now accepted from both (previously the second was DuplicateOrderId).
TEST_F(MatchingEngineTest, R7_SameIdDifferentClientsBothAccepted) {
    auto a = engine.submit(NewOrder{owned_limit(5, Side::Buy, 100, 10,
                                                /*owner=*/1)});
    auto b = engine.submit(NewOrder{owned_limit(5, Side::Buy, 99, 10,
                                                /*owner=*/2)});
    EXPECT_EQ(a.status, EngineResult::Accepted);
    EXPECT_EQ(b.status, EngineResult::Accepted);
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_EQ(engine.tracked_client_count(), 2u);
}

// UNCHANGED: the same id resubmitted by the *same* client is rejected.
TEST_F(MatchingEngineTest, R7_SameIdSameClientRejected) {
    engine.submit(NewOrder{owned_limit(5, Side::Buy, 100, 10, /*owner=*/1)});
    auto dup = engine.submit(NewOrder{owned_limit(5, Side::Sell, 200, 10,
                                                  /*owner=*/1)});
    EXPECT_EQ(dup.status, EngineResult::DuplicateOrderId);
}

// STRICTER THAN BEFORE: a *lower* id after a higher one, same client, is
// rejected — monotonicity, not mere set-membership. (The old set-based
// rule would have accepted id 3 here.)
TEST_F(MatchingEngineTest, R7_LowerIdAfterHigherSameClientRejected) {
    auto hi = engine.submit(NewOrder{owned_limit(10, Side::Buy, 100, 10,
                                                 /*owner=*/1)});
    ASSERT_EQ(hi.status, EngineResult::Accepted);
    auto lo = engine.submit(NewOrder{owned_limit(3, Side::Buy, 99, 10,
                                                 /*owner=*/1)});
    EXPECT_EQ(lo.status, EngineResult::DuplicateOrderId);
    // A different client is unaffected by client 1's watermark.
    auto other = engine.submit(NewOrder{owned_limit(3, Side::Buy, 98, 10,
                                                    /*owner=*/2)});
    EXPECT_EQ(other.status, EngineResult::Accepted);
}

// The equal-to-watermark boundary is a duplicate (<=, not <).
TEST_F(MatchingEngineTest, R7_EqualToWatermarkRejected) {
    engine.submit(NewOrder{owned_limit(7, Side::Buy, 100, 10, /*owner=*/1)});
    auto eq = engine.submit(NewOrder{owned_limit(7, Side::Buy, 99, 10,
                                                 /*owner=*/1)});
    EXPECT_EQ(eq.status, EngineResult::DuplicateOrderId);
}

// A rejected order does NOT advance the watermark (NFR2 — a rejection
// consumes no id), so the same id can still be accepted once it's valid.
TEST_F(MatchingEngineTest, R7_RejectedOrderDoesNotAdvanceWatermark) {
    // Invalid price — rejected before the watermark update.
    auto bad = engine.submit(NewOrder{owned_limit(9, Side::Buy, 0, 10,
                                                  /*owner=*/1)});
    ASSERT_EQ(bad.status, EngineResult::InvalidPrice);
    // Same id, now valid.
    auto good = engine.submit(NewOrder{owned_limit(9, Side::Buy, 100, 10,
                                                   /*owner=*/1)});
    EXPECT_EQ(good.status, EngineResult::Accepted);
}

// Bound demonstration: the research report's 200,000 add+cancel-cycle
// shape. One client cycles a monotonically-increasing id 200k times; the
// book never holds more than one order, and — crucially — the tracking
// structure stays size 1, NOT 200,000. The old ever_seen_ids_ set would
// have grown to 200,000 entries here.
TEST(MatchingEngineWatermarkStress, StructureStaysBoundedOver200kCycles) {
    RecordingEventSink sink;
    MatchingEngine engine{&sink};

    constexpr uint64_t kCycles = 200'000;
    for (uint64_t i = 1; i <= kCycles; ++i) {
        LimitOrder o{OrderId{i}, Side::Buy, Price{100}, Quantity{1}};
        o.owner = ClientId{1};
        auto resp = engine.submit(NewOrder{o});
        ASSERT_EQ(resp.status, EngineResult::Accepted) << "cycle " << i;
        auto cxl = engine.cancel(OrderId{i});
        ASSERT_EQ(cxl.status, EngineResult::Accepted) << "cycle " << i;
    }

    EXPECT_EQ(engine.book().order_count(), 0u);
    // The whole point of R7: bounded by client count (1), not cycles.
    EXPECT_EQ(engine.tracked_client_count(), 1u);

    // Two more clients doing the same → still exactly 3, not 600k.
    for (uint64_t i = 1; i <= 1000; ++i) {
        for (uint64_t client : {2u, 3u}) {
            LimitOrder o{OrderId{i}, Side::Buy, Price{100}, Quantity{1}};
            o.owner = ClientId{client};
            ASSERT_EQ(engine.submit(NewOrder{o}).status,
                      EngineResult::Accepted);
            engine.cancel(OrderId{i});
        }
    }
    EXPECT_EQ(engine.tracked_client_count(), 3u);
}

// ===========================================================================
// Phase 11 T8 (R8) — self-trade prevention is O(1) in book depth.
//
// All Phase 8 STP behaviour tests (SameOwnerCrossRejected...,
// DifferentOwnerCrossProceeds, StpDisabled..., CancelResting ordering)
// still pass unmodified — this task changed cost, not behaviour. The
// tests below add: (a) a correctness check at large depth, and (b) the
// research report's depth-1/10/100/1000 shape, asserting the per-call
// cost no longer scales with depth.
// ===========================================================================

// Build a RejectIncoming-STP engine with an ask book `depth` levels deep,
// all owned by `other`, plus ONE resting ask owned by `self` at the
// highest (deepest to cross) price. Returns that deepest price.
static Price build_deep_ask_book(MatchingEngine& engine, int depth,
                                 uint64_t self_owner, uint64_t other_owner) {
    uint64_t id = 1;
    for (int i = 0; i < depth; ++i) {
        LimitOrder o{OrderId{id++}, Side::Sell,
                     Price{1000 + i}, Quantity{1}};
        o.owner = ClientId{other_owner};
        engine.submit(NewOrder{o});
    }
    const Price deep{1000 + depth};
    LimitOrder mine{OrderId{id++}, Side::Sell, deep, Quantity{1}};
    mine.owner = ClientId{self_owner};
    engine.submit(NewOrder{mine});
    return deep;
}

TEST(StpDepthTest, CorrectAtLargeDepth) {
    RecordingEventSink sink;
    MatchingEngine engine{&sink, 100'000,
                          StpConfig{true, StpPolicy::RejectIncoming}};
    const Price deep =
        build_deep_ask_book(engine, 2000, /*self=*/1, /*other=*/2);

    // A client-1 buy that reaches its own deep resting ask self-crosses,
    // even though 2000 other-owned levels sit in front of it.
    LimitOrder cross{OrderId{999999}, Side::Buy, deep, Quantity{1}};
    cross.owner = ClientId{1};
    EXPECT_EQ(engine.submit(NewOrder{cross}).status,
              EngineResult::SelfTradePrevented);

    // A client-1 buy priced below its own ask does NOT self-cross.
    LimitOrder below{OrderId{999998}, Side::Buy, Price{deep.value - 1},
                     Quantity{1}};
    below.owner = ClientId{1};
    EXPECT_NE(engine.submit(NewOrder{below}).status,
              EngineResult::SelfTradePrevented);

    // A client-3 buy at the deep price crosses (trades) — client 3 has
    // nothing resting; STP is per client.
    LimitOrder other_buy{OrderId{1}, Side::Buy, deep, Quantity{1}};
    other_buy.owner = ClientId{3};
    EXPECT_EQ(engine.submit(NewOrder{other_buy}).status,
              EngineResult::Accepted);
}

TEST(StpDepthTest, CostDoesNotScaleWithDepth) {
    auto time_rejects_at_depth = [](int depth) -> double {
        RecordingEventSink sink;
        MatchingEngine engine{&sink, 2'000'000,
                              StpConfig{true, StpPolicy::RejectIncoming}};
        const Price deep =
            build_deep_ask_book(engine, depth, /*self=*/1, /*other=*/2);

        // Reused id: every probe is STP-rejected before the watermark
        // update, so client 1's watermark never advances and the id
        // stays valid to resubmit.
        LimitOrder probe{OrderId{900000}, Side::Buy, deep, Quantity{1}};
        probe.owner = ClientId{1};
        const NewOrder order{probe};

        constexpr int kIters = 200'000;
        for (int i = 0; i < 1000; ++i) {  // warm-up
            EXPECT_EQ(engine.submit(order).status,
                      EngineResult::SelfTradePrevented);
        }
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i) {
            EXPECT_EQ(engine.submit(order).status,
                      EngineResult::SelfTradePrevented);
        }
        auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::nano>(t1 - t0).count() /
               kIters;
    };

    const double d1 = time_rejects_at_depth(1);
    const double d10 = time_rejects_at_depth(10);
    const double d100 = time_rejects_at_depth(100);
    const double d1000 = time_rejects_at_depth(1000);

    std::printf(
        "[StpDepthTest] ns/reject: depth1=%.1f depth10=%.1f depth100=%.1f "
        "depth1000=%.1f  ratio(1000/1)=%.2fx\n",
        d1, d10, d100, d1000, d1000 / d1);

    // O(depth) would make this ratio ~1000x. O(1) keeps it near 1x;
    // allow generous slack for an unpinned box and the map constant
    // factor — but nowhere near linear in depth.
    EXPECT_LT(d1000 / d1, 8.0)
        << "STP cost still scales with book depth (d1=" << d1
        << "ns, d1000=" << d1000 << "ns)";
}

}  // namespace
}  // namespace miniexchange
