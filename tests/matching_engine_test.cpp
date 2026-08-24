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

    // OrderId{1} is fully filled and no longer resting — but its ID is
    // lifetime-unique per §2.1, so reusing it must be rejected.
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
// This distinguishes "resting" from "lifetime-unique" — the ID is in
// ever_seen_ids_ but NOT in the book, so cancel correctly rejects it.
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

}  // namespace
}  // namespace miniexchange
