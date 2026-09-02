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

// RecordingEventSink — captures all events for integration test assertions.
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
// Integration test fixture.
// ---------------------------------------------------------------------------
class IntegrationTest : public ::testing::Test {
protected:
    RecordingEventSink sink;
    MatchingEngine engine{&sink};
};

// ===========================================================================
// Scenario 1: Build a multi-level book, sweep with a large crossing order.
// Verifies correct trade sequence, remaining book state, EventSink output.
// ===========================================================================
TEST_F(IntegrationTest, SweepMultiLevelBookWithLargeCrossingOrder) {
    // Build the ask side: 5 sell orders at different price levels.
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{15}}});
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Sell, Price{102}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{4}, Side::Sell, Price{103}, Quantity{25}}});
    engine.submit(NewOrder{LimitOrder{OrderId{5}, Side::Sell, Price{104}, Quantity{30}}});

    // Build the bid side: 2 buy orders (non-crossing with the sells).
    engine.submit(NewOrder{LimitOrder{OrderId{6}, Side::Buy, Price{95}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{7}, Side::Buy, Price{94}, Quantity{10}}});

    ASSERT_EQ(engine.book().order_count(), 7u);
    sink.clear();

    // Sweep: a large buy at price 103 for qty 60.
    // Should fill: level 100 (10) + level 101 (15) + level 102 (20) + level 103 (15 partial).
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{8}, Side::Buy, Price{103}, Quantity{60}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 4u);

    // Trade 0: against order 1 at price 100, qty 10
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{1});
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{8});
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});

    // Trade 1: against order 2 at price 101, qty 15
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{2});
    EXPECT_EQ(response.trades[1].price, Price{101});
    EXPECT_EQ(response.trades[1].quantity, Quantity{15});

    // Trade 2: against order 3 at price 102, qty 20
    EXPECT_EQ(response.trades[2].sell_order_id, OrderId{3});
    EXPECT_EQ(response.trades[2].price, Price{102});
    EXPECT_EQ(response.trades[2].quantity, Quantity{20});

    // Trade 3: against order 4 at price 103, qty 15 (partial)
    EXPECT_EQ(response.trades[3].sell_order_id, OrderId{4});
    EXPECT_EQ(response.trades[3].price, Price{103});
    EXPECT_EQ(response.trades[3].quantity, Quantity{15});

    // Incoming buy fully filled.
    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Trade sequences are strictly monotonically increasing.
    for (std::size_t i = 1; i < response.trades.size(); ++i) {
        EXPECT_TRUE(response.trades[i - 1].trade_sequence <
                    response.trades[i].trade_sequence);
    }

    // EventSink received exactly 1 on_order_accepted + 4 on_trade calls.
    EXPECT_EQ(sink.accepted.size(), 1u);
    EXPECT_EQ(sink.accepted[0].id, OrderId{8});
    ASSERT_EQ(sink.trades.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(sink.trades[i].trade_sequence,
                  response.trades[i].trade_sequence);
    }

    // Book state after sweep:
    // - Orders 1,2,3 fully consumed (gone).
    // - Order 4 partially filled: 25 - 15 = 10 remaining.
    // - Order 5 untouched at 104 (price > 103, not crossed).
    // - Orders 6,7 (bids) untouched.
    // Total resting: 4 (order 4 partial + order 5 + orders 6,7).
    EXPECT_EQ(engine.book().order_count(), 4u);
    EXPECT_EQ(engine.book().find_order(OrderId{1}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{2}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{3}), nullptr);

    Order* order4 = engine.book().find_order(OrderId{4});
    ASSERT_NE(order4, nullptr);
    EXPECT_EQ(order4->quantity, Quantity{10});
    EXPECT_EQ(order4->price, Price{103});

    Order* order5 = engine.book().find_order(OrderId{5});
    ASSERT_NE(order5, nullptr);
    EXPECT_EQ(order5->price, Price{104});

    Order* order6 = engine.book().find_order(OrderId{6});
    ASSERT_NE(order6, nullptr);
    EXPECT_EQ(order6->price, Price{95});

    EXPECT_NE(engine.book().find_order(OrderId{7}), nullptr);
}

// ===========================================================================
// Scenario 2: Submit → partial fill → cancel remainder.
// Verifies on_order_cancelled reports the correct remaining after partial fill.
// ===========================================================================
TEST_F(IntegrationTest, PartialFillThenCancelRemainder) {
    // Set up liquidity: 3 sell orders at ascending prices.
    engine.submit(NewOrder{LimitOrder{OrderId{10}, Side::Sell, Price{100}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{11}, Side::Sell, Price{101}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{12}, Side::Sell, Price{102}, Quantity{20}}});

    // Submit a buy that partially fills (crosses level 100 only, then rests).
    // Buy at 100 for qty 50: fills 20 from order 10, rests 30 at price 100.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{13}, Side::Buy, Price{100}, Quantity{50}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].quantity, Quantity{20});
    EXPECT_EQ(response.remaining_qty, Quantity{30});

    // The buy is now resting with qty 30.
    Order* resting_buy = engine.book().find_order(OrderId{13});
    ASSERT_NE(resting_buy, nullptr);
    EXPECT_EQ(resting_buy->quantity, Quantity{30});

    // Now cancel the remainder.
    sink.clear();
    auto cancel_response = engine.cancel(OrderId{13});

    EXPECT_EQ(cancel_response.status, EngineResult::Accepted);
    EXPECT_EQ(cancel_response.remaining_qty, Quantity{30});
    EXPECT_TRUE(cancel_response.trades.empty());

    // EventSink: on_order_cancelled with remaining 30.
    ASSERT_EQ(sink.cancelled.size(), 1u);
    EXPECT_EQ(sink.cancelled[0].id, OrderId{13});
    EXPECT_EQ(sink.cancelled[0].remaining_qty, Quantity{30});

    // Book state: order 13 removed, orders 11 and 12 still resting.
    EXPECT_EQ(engine.book().find_order(OrderId{13}), nullptr);
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_NE(engine.book().find_order(OrderId{11}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{12}), nullptr);
}

// ===========================================================================
// Scenario 3: Interleave limit and market orders in a single scenario.
// Verifies that limit and market orders coexist correctly.
// ===========================================================================
TEST_F(IntegrationTest, InterleavedLimitAndMarketOrders) {
    // Place 3 limit sells.
    engine.submit(NewOrder{LimitOrder{OrderId{20}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{21}, Side::Sell, Price{101}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{22}, Side::Sell, Price{102}, Quantity{10}}});

    // Place a limit buy that doesn't cross (below best ask).
    engine.submit(NewOrder{LimitOrder{OrderId{23}, Side::Buy, Price{98}, Quantity{15}}});

    EXPECT_EQ(engine.book().order_count(), 4u);

    // Market buy: sweeps best ask (100) partially. Qty 5 → fills 5 from order 20.
    sink.clear();
    auto resp1 = engine.submit(
        NewOrder{MarketOrder{OrderId{24}, Side::Buy, Quantity{5}}});

    EXPECT_EQ(resp1.status, EngineResult::Accepted);
    ASSERT_EQ(resp1.trades.size(), 1u);
    EXPECT_EQ(resp1.trades[0].price, Price{100});
    EXPECT_EQ(resp1.trades[0].quantity, Quantity{5});
    EXPECT_EQ(resp1.remaining_qty, Quantity{0});

    // Order 20 still resting with qty 5.
    Order* order20 = engine.book().find_order(OrderId{20});
    ASSERT_NE(order20, nullptr);
    EXPECT_EQ(order20->quantity, Quantity{5});

    // Market order did NOT rest.
    EXPECT_EQ(engine.book().find_order(OrderId{24}), nullptr);
    EXPECT_EQ(engine.book().order_count(), 4u);  // same 4 resting orders

    // Another market buy: sweeps remaining of order 20 (5) and all of order 21 (10).
    // Total fill: 15. Market buy for 15.
    sink.clear();
    auto resp2 = engine.submit(
        NewOrder{MarketOrder{OrderId{25}, Side::Buy, Quantity{15}}});

    EXPECT_EQ(resp2.status, EngineResult::Accepted);
    ASSERT_EQ(resp2.trades.size(), 2u);
    EXPECT_EQ(resp2.trades[0].price, Price{100});
    EXPECT_EQ(resp2.trades[0].quantity, Quantity{5});
    EXPECT_EQ(resp2.trades[0].sell_order_id, OrderId{20});
    EXPECT_EQ(resp2.trades[1].price, Price{101});
    EXPECT_EQ(resp2.trades[1].quantity, Quantity{10});
    EXPECT_EQ(resp2.trades[1].sell_order_id, OrderId{21});
    EXPECT_EQ(resp2.remaining_qty, Quantity{0});

    // Orders 20, 21 fully consumed. 22 and 23 remain.
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_EQ(engine.book().find_order(OrderId{20}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{21}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{22}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{23}), nullptr);

    // Now a limit sell that crosses the resting buy at 98.
    sink.clear();
    auto resp3 = engine.submit(
        NewOrder{LimitOrder{OrderId{26}, Side::Sell, Price{97}, Quantity{15}}});

    EXPECT_EQ(resp3.status, EngineResult::Accepted);
    ASSERT_EQ(resp3.trades.size(), 1u);
    EXPECT_EQ(resp3.trades[0].price, Price{98});  // resting buy's price
    EXPECT_EQ(resp3.trades[0].quantity, Quantity{15});
    EXPECT_EQ(resp3.trades[0].buy_order_id, OrderId{23});
    EXPECT_EQ(resp3.trades[0].sell_order_id, OrderId{26});
    EXPECT_EQ(resp3.remaining_qty, Quantity{0});

    // Order 23 was qty 15, fully consumed. Order 22 still rests. Sell 26 fully filled.
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().find_order(OrderId{23}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{22}), nullptr);

    // EventSink for this last submit: 1 accepted + 1 trade.
    EXPECT_EQ(sink.accepted.size(), 1u);
    EXPECT_EQ(sink.accepted[0].id, OrderId{26});
    ASSERT_EQ(sink.trades.size(), 1u);
    EXPECT_EQ(sink.trades[0].buy_order_id, OrderId{23});
}

// ===========================================================================
// Scenario 4: Fill the book, cancel everything, refill, sweep again.
// Edge-case sequence verifying book state consistency across full lifecycle.
// ===========================================================================
TEST_F(IntegrationTest, FillBookCancelEverythingRefillAndSweep) {
    // Phase A: Fill the book with 5 orders.
    engine.submit(NewOrder{LimitOrder{OrderId{30}, Side::Buy, Price{50}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{31}, Side::Buy, Price{51}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{32}, Side::Sell, Price{60}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{33}, Side::Sell, Price{61}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{34}, Side::Sell, Price{62}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 5u);

    // Phase B: Cancel all orders one by one.
    for (uint64_t id = 30; id <= 34; ++id) {
        auto resp = engine.cancel(OrderId{id});
        EXPECT_EQ(resp.status, EngineResult::Accepted);
    }

    EXPECT_EQ(engine.book().order_count(), 0u);
    // All orders gone — no resting orders remain on either side.

    // Phase C: cancelled order IDs (30-34) are all <= this client's
    // monotonic watermark (34), so re-submitting them is rejected
    // (Phase 11 R7 — per-client monotonic uniqueness).
    for (uint64_t id = 30; id <= 34; ++id) {
        auto resp = engine.submit(
            NewOrder{LimitOrder{OrderId{id}, Side::Buy, Price{100}, Quantity{1}}});
        EXPECT_EQ(resp.status, EngineResult::DuplicateOrderId);
    }

    // Phase D: Refill with fresh IDs.
    engine.submit(NewOrder{LimitOrder{OrderId{40}, Side::Sell, Price{100}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{41}, Side::Sell, Price{101}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{42}, Side::Sell, Price{102}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{43}, Side::Buy, Price{90}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{44}, Side::Buy, Price{89}, Quantity{5}}});

    EXPECT_EQ(engine.book().order_count(), 5u);

    // Phase E: Sweep all asks with a large buy.
    sink.clear();
    auto sweep_resp = engine.submit(
        NewOrder{LimitOrder{OrderId{45}, Side::Buy, Price{102}, Quantity{15}}});

    EXPECT_EQ(sweep_resp.status, EngineResult::Accepted);
    ASSERT_EQ(sweep_resp.trades.size(), 3u);
    EXPECT_EQ(sweep_resp.trades[0].price, Price{100});
    EXPECT_EQ(sweep_resp.trades[1].price, Price{101});
    EXPECT_EQ(sweep_resp.trades[2].price, Price{102});
    EXPECT_EQ(sweep_resp.remaining_qty, Quantity{0});

    // All sells consumed. Only 2 bids remain.
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_EQ(engine.book().find_order(OrderId{40}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{41}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{42}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{43}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{44}), nullptr);
}

// ===========================================================================
// Scenario 5: Self-crossing — same "client" submits a buy and a sell that
// cross each other. Per R14, this matches normally (no self-trade prevention).
// ===========================================================================
TEST_F(IntegrationTest, SelfCrossingMultipleOrdersMatchNormally) {
    // "Client A" submits multiple sells.
    engine.submit(NewOrder{LimitOrder{OrderId{50}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{51}, Side::Sell, Price{101}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{52}, Side::Sell, Price{102}, Quantity{10}}});

    // Also place some bids from "client A" (non-crossing).
    engine.submit(NewOrder{LimitOrder{OrderId{53}, Side::Buy, Price{90}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{54}, Side::Buy, Price{91}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 5u);
    sink.clear();

    // "Client A" submits a buy that crosses their own sells — no prevention.
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{55}, Side::Buy, Price{101}, Quantity{20}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 2u);

    // Fills: order 50 (qty 10 at 100) then order 51 (qty 10 at 101).
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId{55});
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{50});
    EXPECT_EQ(response.trades[0].price, Price{100});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});

    EXPECT_EQ(response.trades[1].buy_order_id, OrderId{55});
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{51});
    EXPECT_EQ(response.trades[1].price, Price{101});
    EXPECT_EQ(response.trades[1].quantity, Quantity{10});

    EXPECT_EQ(response.remaining_qty, Quantity{0});

    // Orders 50, 51 consumed. Remaining: 52 (sell), 53, 54 (buys).
    EXPECT_EQ(engine.book().order_count(), 3u);
    EXPECT_EQ(engine.book().find_order(OrderId{50}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{51}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{52}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{53}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{54}), nullptr);

    // EventSink received correct events.
    EXPECT_EQ(sink.accepted.size(), 1u);
    ASSERT_EQ(sink.trades.size(), 2u);
    EXPECT_EQ(sink.trades[0].sell_order_id, OrderId{50});
    EXPECT_EQ(sink.trades[1].sell_order_id, OrderId{51});

    // Now "Client A" also crosses their own bids with a sell.
    sink.clear();
    auto response2 = engine.submit(
        NewOrder{LimitOrder{OrderId{56}, Side::Sell, Price{90}, Quantity{20}}});

    EXPECT_EQ(response2.status, EngineResult::Accepted);
    ASSERT_EQ(response2.trades.size(), 2u);

    // Best bid is 91 (order 54), then 90 (order 53).
    EXPECT_EQ(response2.trades[0].buy_order_id, OrderId{54});
    EXPECT_EQ(response2.trades[0].price, Price{91});
    EXPECT_EQ(response2.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response2.trades[1].buy_order_id, OrderId{53});
    EXPECT_EQ(response2.trades[1].price, Price{90});
    EXPECT_EQ(response2.trades[1].quantity, Quantity{10});
    EXPECT_EQ(response2.remaining_qty, Quantity{0});

    // Only order 52 remains.
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_NE(engine.book().find_order(OrderId{52}), nullptr);
}

// ===========================================================================
// Scenario 6: book() read-only accessor consistency through a complex lifecycle.
// Verifies order_count() and book state after each operation.
// ===========================================================================
TEST_F(IntegrationTest, BookAccessorConsistencyThroughLifecycle) {
    // Step 1: Empty book.
    EXPECT_EQ(engine.book().order_count(), 0u);

    // Step 2: Add 3 buys at different prices.
    engine.submit(NewOrder{LimitOrder{OrderId{60}, Side::Buy, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{61}, Side::Buy, Price{99}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{62}, Side::Buy, Price{98}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 3u);
    // Verify prices by finding orders.
    Order* o60 = engine.book().find_order(OrderId{60});
    ASSERT_NE(o60, nullptr);
    EXPECT_EQ(o60->price, Price{100});  // highest bid
    EXPECT_NE(engine.book().find_order(OrderId{61}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{62}), nullptr);

    // Step 3: Add 3 sells.
    engine.submit(NewOrder{LimitOrder{OrderId{63}, Side::Sell, Price{110}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{64}, Side::Sell, Price{111}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{65}, Side::Sell, Price{112}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 6u);

    // Step 4: Cancel the best bid (order 60, price 100).
    engine.cancel(OrderId{60});
    EXPECT_EQ(engine.book().order_count(), 5u);
    EXPECT_EQ(engine.book().find_order(OrderId{60}), nullptr);
    // New best bid should be order 61 at price 99.
    Order* o61 = engine.book().find_order(OrderId{61});
    ASSERT_NE(o61, nullptr);
    EXPECT_EQ(o61->price, Price{99});

    // Step 5: Cancel the best ask (order 63, price 110).
    engine.cancel(OrderId{63});
    EXPECT_EQ(engine.book().order_count(), 4u);
    EXPECT_EQ(engine.book().find_order(OrderId{63}), nullptr);
    // New best ask should be order 64 at price 111.
    Order* o64 = engine.book().find_order(OrderId{64});
    ASSERT_NE(o64, nullptr);
    EXPECT_EQ(o64->price, Price{111});

    // Step 6: Crossing buy that sweeps remaining asks (111 and 112).
    auto resp = engine.submit(
        NewOrder{LimitOrder{OrderId{66}, Side::Buy, Price{112}, Quantity{20}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    ASSERT_EQ(resp.trades.size(), 2u);
    EXPECT_EQ(resp.remaining_qty, Quantity{0});

    // Both asks consumed (64, 65). 2 buys remain (61, 62).
    EXPECT_EQ(engine.book().order_count(), 2u);
    EXPECT_EQ(engine.book().find_order(OrderId{64}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{65}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{61}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{62}), nullptr);

    // Step 7: Add a new ask and verify.
    engine.submit(NewOrder{LimitOrder{OrderId{67}, Side::Sell, Price{200}, Quantity{5}}});
    EXPECT_EQ(engine.book().order_count(), 3u);
    Order* o67 = engine.book().find_order(OrderId{67});
    ASSERT_NE(o67, nullptr);
    EXPECT_EQ(o67->price, Price{200});

    // Step 8: Market sell sweeps both remaining bids.
    auto resp2 = engine.submit(
        NewOrder{MarketOrder{OrderId{68}, Side::Sell, Quantity{25}}});
    EXPECT_EQ(resp2.status, EngineResult::Accepted);
    ASSERT_EQ(resp2.trades.size(), 2u);
    EXPECT_EQ(resp2.trades[0].price, Price{99});   // best bid first
    EXPECT_EQ(resp2.trades[1].price, Price{98});
    // Filled 10+10=20, remaining 5 discarded (market never rests).
    EXPECT_EQ(resp2.remaining_qty, Quantity{5});

    // Only sell order 67 remains.
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().find_order(OrderId{61}), nullptr);
    EXPECT_EQ(engine.book().find_order(OrderId{62}), nullptr);
    EXPECT_NE(engine.book().find_order(OrderId{67}), nullptr);
}

// ===========================================================================
// Scenario 7: Multiple orders at same price level with interleaved cancels
// and fills. Tests FIFO ordering and order_count consistency.
// ===========================================================================
TEST_F(IntegrationTest, SamePriceLevelFIFOWithCancelsAndFills) {
    // Place 5 sell orders at price 100.
    engine.submit(NewOrder{LimitOrder{OrderId{70}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{71}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{72}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{73}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{74}, Side::Sell, Price{100}, Quantity{10}}});

    EXPECT_EQ(engine.book().order_count(), 5u);

    // Cancel the middle order (72) and the first order (70).
    engine.cancel(OrderId{72});
    engine.cancel(OrderId{70});
    EXPECT_EQ(engine.book().order_count(), 3u);

    // FIFO order at this level is now: 71, 73, 74.
    // Send a crossing buy for 25 to sweep them all.
    sink.clear();
    auto response = engine.submit(
        NewOrder{LimitOrder{OrderId{75}, Side::Buy, Price{100}, Quantity{25}}});

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);

    // FIFO: 71 first, then 73, then 74.
    EXPECT_EQ(response.trades[0].sell_order_id, OrderId{71});
    EXPECT_EQ(response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(response.trades[1].sell_order_id, OrderId{73});
    EXPECT_EQ(response.trades[1].quantity, Quantity{10});
    EXPECT_EQ(response.trades[2].sell_order_id, OrderId{74});
    EXPECT_EQ(response.trades[2].quantity, Quantity{5});

    // Remaining: buy fully filled (0), order 74 has 5 left.
    EXPECT_EQ(response.remaining_qty, Quantity{0});
    EXPECT_EQ(engine.book().order_count(), 1u);

    Order* remaining = engine.book().find_order(OrderId{74});
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->quantity, Quantity{5});

    // Verify that only order 74 remains, confirming level consistency.
    EXPECT_EQ(remaining->price, Price{100});
}

// ===========================================================================
// Scenario 8: Large-scale scenario with market orders sweeping an already
// partially-consumed book, verifying event counts and final state.
// ===========================================================================
TEST_F(IntegrationTest, LargeScaleMarketSweepAfterPartialConsumption) {
    // Build a deep bid side: 6 orders across 3 levels.
    engine.submit(NewOrder{LimitOrder{OrderId{80}, Side::Buy, Price{50}, Quantity{100}}});
    engine.submit(NewOrder{LimitOrder{OrderId{81}, Side::Buy, Price{50}, Quantity{100}}});
    engine.submit(NewOrder{LimitOrder{OrderId{82}, Side::Buy, Price{49}, Quantity{100}}});
    engine.submit(NewOrder{LimitOrder{OrderId{83}, Side::Buy, Price{49}, Quantity{100}}});
    engine.submit(NewOrder{LimitOrder{OrderId{84}, Side::Buy, Price{48}, Quantity{100}}});
    engine.submit(NewOrder{LimitOrder{OrderId{85}, Side::Buy, Price{48}, Quantity{100}}});

    EXPECT_EQ(engine.book().order_count(), 6u);

    // Partially consume top level with a limit sell that partially fills.
    auto resp1 = engine.submit(
        NewOrder{LimitOrder{OrderId{86}, Side::Sell, Price{50}, Quantity{150}}});
    EXPECT_EQ(resp1.status, EngineResult::Accepted);
    ASSERT_EQ(resp1.trades.size(), 2u);
    // Fills: order 80 (100) + order 81 (50 partial).
    EXPECT_EQ(resp1.trades[0].quantity, Quantity{100});
    EXPECT_EQ(resp1.trades[0].buy_order_id, OrderId{80});
    EXPECT_EQ(resp1.trades[1].quantity, Quantity{50});
    EXPECT_EQ(resp1.trades[1].buy_order_id, OrderId{81});
    EXPECT_EQ(resp1.remaining_qty, Quantity{0});

    // Book state: order 80 fully consumed. Order 81 has 50 remaining.
    // Total: 81(50) + 82(100) + 83(100) + 84(100) + 85(100) = 5 orders.
    EXPECT_EQ(engine.book().order_count(), 5u);
    Order* order81 = engine.book().find_order(OrderId{81});
    ASSERT_NE(order81, nullptr);
    EXPECT_EQ(order81->quantity, Quantity{50});

    // Now sweep with a large market sell. Qty 350 → should fill:
    //   order 81 (50 @ 50), order 82 (100 @ 49), order 83 (100 @ 49),
    //   order 84 (100 @ 48). That's 350 total.
    sink.clear();
    auto resp2 = engine.submit(
        NewOrder{MarketOrder{OrderId{87}, Side::Sell, Quantity{350}}});

    EXPECT_EQ(resp2.status, EngineResult::Accepted);
    ASSERT_EQ(resp2.trades.size(), 4u);

    EXPECT_EQ(resp2.trades[0].buy_order_id, OrderId{81});
    EXPECT_EQ(resp2.trades[0].price, Price{50});
    EXPECT_EQ(resp2.trades[0].quantity, Quantity{50});

    EXPECT_EQ(resp2.trades[1].buy_order_id, OrderId{82});
    EXPECT_EQ(resp2.trades[1].price, Price{49});
    EXPECT_EQ(resp2.trades[1].quantity, Quantity{100});

    EXPECT_EQ(resp2.trades[2].buy_order_id, OrderId{83});
    EXPECT_EQ(resp2.trades[2].price, Price{49});
    EXPECT_EQ(resp2.trades[2].quantity, Quantity{100});

    EXPECT_EQ(resp2.trades[3].buy_order_id, OrderId{84});
    EXPECT_EQ(resp2.trades[3].price, Price{48});
    EXPECT_EQ(resp2.trades[3].quantity, Quantity{100});

    EXPECT_EQ(resp2.remaining_qty, Quantity{0});

    // Only order 85 remains (100 @ 48).
    EXPECT_EQ(engine.book().order_count(), 1u);
    Order* order85 = engine.book().find_order(OrderId{85});
    ASSERT_NE(order85, nullptr);
    EXPECT_EQ(order85->quantity, Quantity{100});

    // EventSink: 1 accepted + 4 trades.
    EXPECT_EQ(sink.accepted.size(), 1u);
    EXPECT_EQ(sink.accepted[0].id, OrderId{87});
    ASSERT_EQ(sink.trades.size(), 4u);

    // Verify trade sequences are monotonically increasing.
    for (std::size_t i = 1; i < sink.trades.size(); ++i) {
        EXPECT_TRUE(sink.trades[i - 1].trade_sequence <
                    sink.trades[i].trade_sequence);
    }
}

}  // namespace
}  // namespace miniexchange

