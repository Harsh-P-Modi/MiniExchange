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

    void clear() {
        trades.clear();
        accepted.clear();
        cancelled.clear();
    }

    std::vector<Trade> trades;
    std::vector<OrderAccepted> accepted;
    std::vector<OrderCancelled> cancelled;
};

// ---------------------------------------------------------------------------
// Test fixture: creates a MatchingEngine with a RecordingEventSink.
// ---------------------------------------------------------------------------
class EdgeCaseTest : public ::testing::Test {
protected:
    RecordingEventSink sink;
    MatchingEngine engine{&sink};
};

// ===========================================================================
// Zero-quantity limit order — confirm InvalidQuantity is returned.
// (Already covered by Task 9 tests, but explicitly confirmed here.)
// ===========================================================================
TEST_F(EdgeCaseTest, ZeroQuantityLimitOrderRejected) {
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{0}}});

    EXPECT_EQ(response.status, EngineResult::InvalidQuantity);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);

    // No events emitted for rejection.
    EXPECT_EQ(sink.accepted.size(), 0u);
    EXPECT_EQ(sink.trades.size(), 0u);
}

// ===========================================================================
// Single tick (price=1, quantity=1) orders matching correctly.
// ===========================================================================
TEST_F(EdgeCaseTest, SingleTickOrdersMatchCorrectly) {
    // Place a sell at price 1, qty 1 — the minimum valid order.
    engine.submit(NewOrder{LimitOrder{OrderId{10}, Side::Sell, Price{1}, Quantity{1}}});

    // Buy at price 1, qty 1 — should cross and fully fill both.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{11}, Side::Buy, Price{1}, Quantity{1}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].price, Price{1});
    EXPECT_EQ(response.trades[0].quantity, Quantity{1});
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{11});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{10});
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Book empty after full fill.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(EdgeCaseTest, SingleTickOrderRestsCorrectly) {
    // A single-tick order that doesn't cross should rest normally.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{12}, Side::Buy, Price{1}, Quantity{1}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{1});
    EXPECT_EQ(engine.book().order_count(), 1u);

    Order* resting = engine.book().find_order(OrderId{12});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->price, Price{1});
    EXPECT_EQ(resting->quantity, Quantity{1});
}

// ===========================================================================
// Large number of orders at the same price level (1000 orders).
// Confirms correctness, not performance.
// ===========================================================================
TEST_F(EdgeCaseTest, ThousandOrdersAtSamePriceLevel) {
    constexpr uint64_t kNumOrders = 1000;

    // Place 1000 sell orders at price 100, qty 1 each.
    for (uint64_t i = 0; i < kNumOrders; ++i) {
        auto response = engine.submit(
            NewOrder{LimitOrder{OrderId{i + 1}, Side::Sell, Price{100}, Quantity{1}}});
        ASSERT_EQ(response.status, EngineResult::Accepted) << "Failed at order " << i;
    }

    EXPECT_EQ(engine.book().order_count(), kNumOrders);

    // Verify best ask has total_quantity = 1000.
    const auto& asks = engine.book().asks();
    ASSERT_FALSE(asks.empty());
    const PriceLevel& best_ask = asks.begin()->second;
    EXPECT_EQ(best_ask.price(), Price{100});
    EXPECT_EQ(best_ask.total_quantity(), Quantity{kNumOrders});

    // Sweep all 1000 with a single buy.
    sink.clear();
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{kNumOrders + 1}, Side::Buy, Price{100},
                            Quantity{kNumOrders}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), kNumOrders);
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);

    // Verify trades are in FIFO order — order IDs 1 through 1000.
    for (uint64_t i = 0; i < kNumOrders; ++i) {
        EXPECT_EQ(response.trades[i].sell_order_id, OrderId{i + 1})
            << "FIFO violated at trade " << i;
        EXPECT_EQ(response.trades[i].quantity, Quantity{1});
    }

    // EventSink saw exactly 1000 trades.
    EXPECT_EQ(sink.trades.size(), kNumOrders);
}

// ===========================================================================
// Cancel on an empty book → UnknownOrderId.
// ===========================================================================
TEST_F(EdgeCaseTest, CancelOnEmptyBookReturnsUnknownOrderId) {
    // Book is empty — no orders ever submitted.
    EXPECT_EQ(engine.book().order_count(), 0u);

    auto response = engine.cancel(OrderId{42});

    EXPECT_EQ(response.status, EngineResult::UnknownOrderId);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // No events emitted.
    EXPECT_EQ(sink.cancelled.size(), 0u);
}

// ===========================================================================
// Market order that exactly exhausts available liquidity (remaining = 0).
// ===========================================================================
TEST_F(EdgeCaseTest, MarketOrderExactlyExhaustsLiquidity) {
    // Place 3 sell orders: total liquidity = 10 + 20 + 30 = 60.
    engine.submit(NewOrder{LimitOrder{OrderId{50}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{51}, Side::Sell, Price{101}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{52}, Side::Sell, Price{102}, Quantity{30}}});

    // Market buy for exactly 60 — should exhaust all liquidity.
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{53}, Side::Buy, Quantity{60}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);

    // Verify fills at each level.
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[1].price, Price{101});
    EXPECT_EQ(response.trades[1].quantity, Quantity{20});
    EXPECT_EQ(response.trades[2].price, Price{102});
    EXPECT_EQ(response.trades[2].quantity, Quantity{30});
}

// ===========================================================================
// Rapidly alternating add/cancel/add/cancel sequences — verify no stale
// pointers or book corruption.
// ===========================================================================
TEST_F(EdgeCaseTest, RapidAddCancelSequencesNoCorruption) {
    constexpr uint64_t kIterations = 200;

    for (uint64_t i = 0; i < kIterations; ++i) {
        OrderId id{i + 1000};

        // Add an order.
        auto add_resp = engine.submit(
            NewOrder{LimitOrder{id, Side::Buy, Price{50}, Quantity{10}}});
        ASSERT_EQ(add_resp.status, EngineResult::Accepted) << "Add failed at iter " << i;
        ASSERT_EQ(engine.book().order_count(), 1u) << "Count wrong after add at iter " << i;

        // Immediately cancel it.
        auto cancel_resp = engine.cancel(id);
        ASSERT_EQ(cancel_resp.status, EngineResult::Accepted) << "Cancel failed at iter " << i;
        ASSERT_EQ(engine.book().order_count(), 0u) << "Count wrong after cancel at iter " << i;
    }

    // Final state: book is empty; the client's monotonic watermark now
    // sits at the highest id used (1199).
    EXPECT_EQ(engine.book().order_count(), 0u);

    // Verify an earlier id can't be reused (id 1000 <= watermark 1199).
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{1000}, Side::Sell, Price{100}, Quantity{1}}});
    EXPECT_EQ(resp.status, EngineResult::DuplicateOrderId);
}

TEST_F(EdgeCaseTest, RapidAddCancelWithMultipleOrdersOnSameLevel) {
    // Add multiple orders at same price, cancel them in various orders,
    // then verify the book is consistent.
    engine.submit(NewOrder{LimitOrder{OrderId{2000}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2001}, Side::Sell, Price{100}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2002}, Side::Sell, Price{100}, Quantity{30}}});

    EXPECT_EQ(engine.book().order_count(), 3u);

    // Cancel the middle order.
    engine.cancel(OrderId{2001});
    EXPECT_EQ(engine.book().order_count(), 2u);

    // Add another at the same price.
    engine.submit(NewOrder{LimitOrder{OrderId{2003}, Side::Sell, Price{100}, Quantity{5}}});
    EXPECT_EQ(engine.book().order_count(), 3u);

    // Cancel the first order.
    engine.cancel(OrderId{2000});
    EXPECT_EQ(engine.book().order_count(), 2u);

    // Now match against remaining: order 2002 (30) then 2003 (5) in FIFO.
    sink.clear();
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{2004}, Side::Buy, Price{100}, Quantity{35}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 2u);
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{2002});
    EXPECT_EQ(response.trades[0].quantity, Quantity{30});
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{2003});
    EXPECT_EQ(response.trades[1].quantity, Quantity{5});
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ===========================================================================
// An incoming order whose price exactly equals the best opposite price
// → it SHOULD cross (verify).
// ===========================================================================
TEST_F(EdgeCaseTest, ExactPriceEqualsCrosses_BuySide) {
    // Resting sell at price 100.
    engine.submit(NewOrder{LimitOrder{OrderId{300}, Side::Sell, Price{100}, Quantity{10}}});

    // Buy at exactly price 100 — should cross (buy price >= best ask price).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{301}, Side::Buy, Price{100}, Quantity{10}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(EdgeCaseTest, ExactPriceEqualsCrosses_SellSide) {
    // Resting buy at price 100.
    engine.submit(NewOrder{LimitOrder{OrderId{310}, Side::Buy, Price{100}, Quantity{10}}});

    // Sell at exactly price 100 — should cross (sell price <= best bid price).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{311}, Side::Sell, Price{100}, Quantity{10}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(EdgeCaseTest, ExactPriceEqualsCrosses_PartialFill) {
    // Resting sell at price 50, qty 20.
    engine.submit(NewOrder{LimitOrder{OrderId{320}, Side::Sell, Price{50}, Quantity{20}}});

    // Buy at exactly 50, qty 10 — crosses, partially fills the sell.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{321}, Side::Buy, Price{50}, Quantity{10}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].price, Price{50});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Sell still resting with qty 10.
    Order* resting = engine.book().find_order(OrderId{320});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->quantity, Quantity{10});
    EXPECT_EQ(engine.book().order_count(), 1u);
}

// ===========================================================================
// PriceLevel::total_quantity() correctness after partial fills.
// This test specifically validates the bug fix for stale total_qty_.
// ===========================================================================
TEST_F(EdgeCaseTest, TotalQuantityCorrectAfterPartialFill) {
    // Place 3 sells at price 100: qty 20, 30, 50. Total = 100.
    engine.submit(NewOrder{LimitOrder{OrderId{400}, Side::Sell, Price{100}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{401}, Side::Sell, Price{100}, Quantity{30}}});
    engine.submit(NewOrder{LimitOrder{OrderId{402}, Side::Sell, Price{100}, Quantity{50}}});

    {
        const auto& asks = engine.book().asks();
        ASSERT_FALSE(asks.empty());
        const PriceLevel& ask_level = asks.begin()->second;
        EXPECT_EQ(ask_level.price(), Price{100});
        EXPECT_EQ(ask_level.total_quantity(), Quantity{100});
    }

    // Buy qty 25: fully fills order 400 (20), partially fills order 401 (5).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{403}, Side::Buy, Price{100}, Quantity{25}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 2u);
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // After partial fill: order 401 has 25 remaining, order 402 has 50.
    // Total at this level should be 25 + 50 = 75.
    {
        const auto& asks = engine.book().asks();
        ASSERT_FALSE(asks.empty());
        const PriceLevel& ask_level = asks.begin()->second;
        EXPECT_EQ(ask_level.price(), Price{100});
        EXPECT_EQ(ask_level.total_quantity(), Quantity{75});
    }

    // Verify individual order quantities.
    EXPECT_EQ(engine.book().find_order(OrderId{400}), nullptr);  // fully consumed
    Order* order401 = engine.book().find_order(OrderId{401});
    ASSERT_NE(order401, nullptr);
    EXPECT_EQ(order401->quantity, Quantity{25});
    Order* order402 = engine.book().find_order(OrderId{402});
    ASSERT_NE(order402, nullptr);
    EXPECT_EQ(order402->quantity, Quantity{50});
}

TEST_F(EdgeCaseTest, TotalQuantityCorrectAfterMultiplePartialFills) {
    // Place 1 sell at price 100, qty 100.
    engine.submit(NewOrder{LimitOrder{OrderId{410}, Side::Sell, Price{100}, Quantity{100}}});

    {
        const auto& asks = engine.book().asks();
        ASSERT_FALSE(asks.empty());
        EXPECT_EQ(asks.begin()->second.total_quantity(), Quantity{100});
    }

    // Partially fill it 5 times: each buy takes 10.
    for (uint64_t i = 0; i < 5; ++i) {
        auto response = engine.submit(
            NewOrder{LimitOrder{OrderId{411 + i}, Side::Buy, Price{100}, Quantity{10}}});
        ASSERT_EQ(response.status, EngineResult::Accepted);
        ASSERT_EQ(response.trades.size(), 1u);
        ASSERT_EQ(response.remaining_qty, Quantity{0});

        // Total quantity should decrease by 10 each time.
        const auto& asks = engine.book().asks();
        ASSERT_FALSE(asks.empty());
        EXPECT_EQ(asks.begin()->second.total_quantity(), Quantity{100 - 10 * (i + 1)})
            << "total_quantity wrong after partial fill " << i;
    }

    // Remaining: 50 at the level.
    {
        const auto& asks = engine.book().asks();
        ASSERT_FALSE(asks.empty());
        EXPECT_EQ(asks.begin()->second.total_quantity(), Quantity{50});
    }

    // The sell order has qty 50 remaining.
    Order* resting = engine.book().find_order(OrderId{410});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->quantity, Quantity{50});
}

TEST_F(EdgeCaseTest, TotalQuantityZeroAfterFullConsumption) {
    // Place 2 sells at price 100.
    engine.submit(NewOrder{LimitOrder{OrderId{420}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{421}, Side::Sell, Price{100}, Quantity{10}}});

    // Fully consume both with a market buy.
    auto response = engine.submit(
        NewOrder{MarketOrder{OrderId{422}, Side::Buy, Quantity{20}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Level should be removed entirely (asks empty).
    EXPECT_TRUE(engine.book().asks().empty());
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ===========================================================================
// Bid-side total_quantity correctness after partial fill.
// ===========================================================================
TEST_F(EdgeCaseTest, BidSideTotalQuantityCorrectAfterPartialFill) {
    // Place 2 buys at price 50: qty 40, 60. Total = 100.
    engine.submit(NewOrder{LimitOrder{OrderId{430}, Side::Buy, Price{50}, Quantity{40}}});
    engine.submit(NewOrder{LimitOrder{OrderId{431}, Side::Buy, Price{50}, Quantity{60}}});

    {
        const auto& bids = engine.book().bids();
        ASSERT_FALSE(bids.empty());
        EXPECT_EQ(bids.begin()->second.total_quantity(), Quantity{100});
    }

    // Sell at 50, qty 55: fully fills order 430 (40), partially fills order 431 (15).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{432}, Side::Sell, Price{50}, Quantity{55}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 2u);
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Total at bid level 50 should be 60 - 15 = 45.
    {
        const auto& bids = engine.book().bids();
        ASSERT_FALSE(bids.empty());
        EXPECT_EQ(bids.begin()->second.price(), Price{50});
        EXPECT_EQ(bids.begin()->second.total_quantity(), Quantity{45});
    }

    Order* order431 = engine.book().find_order(OrderId{431});
    ASSERT_NE(order431, nullptr);
    EXPECT_EQ(order431->quantity, Quantity{45});
}

}  // namespace
}  // namespace miniexchange
