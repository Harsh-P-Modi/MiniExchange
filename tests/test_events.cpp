#include "core/Events.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include <gtest/gtest.h>

using namespace miniexchange;

// Task 5 acceptance criteria: header compiles; a test constructs each
// type and confirms field access.

TEST(EventsTest, EngineResultEnumValues) {
    // Confirm all enum values are accessible
    EngineResult r1 = EngineResult::Accepted;
    EngineResult r2 = EngineResult::DuplicateOrderId;
    EngineResult r3 = EngineResult::UnknownOrderId;
    EngineResult r4 = EngineResult::InvalidQuantity;
    EngineResult r5 = EngineResult::InvalidPrice;
    EngineResult r6 = EngineResult::PoolExhausted;

    // Silence unused variable warnings
    (void)r1; (void)r2; (void)r3; (void)r4; (void)r5; (void)r6;

    // Confirm enum values are distinct
    EXPECT_NE(EngineResult::Accepted, EngineResult::DuplicateOrderId);
    EXPECT_NE(EngineResult::Accepted, EngineResult::UnknownOrderId);
    EXPECT_NE(EngineResult::Accepted, EngineResult::PoolExhausted);
}

TEST(EventsTest, EngineResponseConstruction) {
    // Construct an EngineResponse with all fields
    Trade trade{
        TradeSequence(1),
        OrderId(100),
        OrderId(200),
        Price(10000),
        Quantity(50)
    };

    EngineResponse response{
        EngineResult::Accepted,
        {trade},  // vector<Trade> with one trade
        Quantity(0)
    };

    // Verify field access
    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 1u);
    EXPECT_EQ(response.trades[0].trade_sequence, TradeSequence(1));
    EXPECT_EQ(response.trades[0].buy_order_id, OrderId(100));
    EXPECT_EQ(response.remaining_qty, Quantity(0));
}

TEST(EventsTest, EngineResponseEmptyTrades) {
    // Response with no trades (rejection case)
    EngineResponse response{
        EngineResult::InvalidQuantity,
        {},  // empty trades vector
        Quantity(0)
    };

    EXPECT_EQ(response.status, EngineResult::InvalidQuantity);
    EXPECT_TRUE(response.trades.empty());
    EXPECT_EQ(response.remaining_qty, Quantity(0));
}

TEST(EventsTest, OrderAcceptedConstruction) {
    OrderAccepted event{
        OrderId(42),
        Side::Buy,
        Quantity(100)
    };

    // Verify field access
    EXPECT_EQ(event.id, OrderId(42));
    EXPECT_EQ(event.side, Side::Buy);
    EXPECT_EQ(event.quantity, Quantity(100));
}

TEST(EventsTest, OrderCancelledConstruction) {
    OrderCancelled event{
        OrderId(99),
        Quantity(75)
    };

    // Verify field access
    EXPECT_EQ(event.id, OrderId(99));
    EXPECT_EQ(event.remaining_qty, Quantity(75));
}

TEST(EventsTest, MultipleTradesInResponse) {
    // Response with multiple fills (incoming order crossed multiple
    // resting orders)
    std::vector<Trade> trades;
    trades.push_back({TradeSequence(10), OrderId(1), OrderId(2), Price(100), Quantity(10)});
    trades.push_back({TradeSequence(11), OrderId(1), OrderId(3), Price(99), Quantity(20)});
    trades.push_back({TradeSequence(12), OrderId(1), OrderId(4), Price(98), Quantity(15)});

    EngineResponse response{
        EngineResult::Accepted,
        trades,
        Quantity(5)  // partial fill, 5 remaining
    };

    EXPECT_EQ(response.status, EngineResult::Accepted);
    ASSERT_EQ(response.trades.size(), 3u);
    EXPECT_EQ(response.trades[0].quantity, Quantity(10));
    EXPECT_EQ(response.trades[1].quantity, Quantity(20));
    EXPECT_EQ(response.trades[2].quantity, Quantity(15));
    EXPECT_EQ(response.remaining_qty, Quantity(5));
}
