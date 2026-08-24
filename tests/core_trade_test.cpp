#include "core/Trade.hpp"

#include <gtest/gtest.h>

using namespace miniexchange;

TEST(CoreTradeTest, ConstructAndReadFields) {
    // Construct a Trade with all fields specified
    Trade trade{
        TradeSequence{42},
        OrderId{1001},     // buy_order_id
        OrderId{2002},     // sell_order_id
        Price{10000},
        Quantity{50}
    };

    // Verify all fields are readable and correct
    EXPECT_EQ(trade.trade_sequence.value, 42u);
    EXPECT_EQ(trade.buy_order_id.value, 1001u);
    EXPECT_EQ(trade.sell_order_id.value, 2002u);
    EXPECT_EQ(trade.price.value, 10000);
    EXPECT_EQ(trade.quantity.value, 50u);
}

TEST(CoreTradeTest, MultipleTradesDistinct) {
    Trade trade1{
        TradeSequence{1},
        OrderId{100},
        OrderId{200},
        Price{9950},
        Quantity{10}
    };

    Trade trade2{
        TradeSequence{2},
        OrderId{300},
        OrderId{400},
        Price{9960},
        Quantity{25}
    };

    // Verify trades maintain distinct values
    EXPECT_NE(trade1.trade_sequence.value, trade2.trade_sequence.value);
    EXPECT_NE(trade1.buy_order_id.value, trade2.buy_order_id.value);
    EXPECT_NE(trade1.sell_order_id.value, trade2.sell_order_id.value);
    EXPECT_NE(trade1.price.value, trade2.price.value);
    EXPECT_NE(trade1.quantity.value, trade2.quantity.value);
}
