#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <type_traits>
#include <unordered_set>

using namespace miniexchange;

// Compile-time type-safety verification: these should NOT compile if
// uncommented, demonstrating that our strong types prevent accidental mixing.
// We verify this via static_assert with type traits instead.

TEST(CoreTypesTest, NoImplicitConversion) {
    // Verify that Price and Quantity are distinct types that don't
    // implicitly convert to each other
    static_assert(!std::is_convertible_v<Price, Quantity>,
                  "Price should not implicitly convert to Quantity");
    static_assert(!std::is_convertible_v<Quantity, Price>,
                  "Quantity should not implicitly convert to Price");

    // Verify that OrderId doesn't convert to numeric types
    static_assert(!std::is_convertible_v<OrderId, Quantity>,
                  "OrderId should not implicitly convert to Quantity");
    static_assert(!std::is_convertible_v<OrderId, Price>,
                  "OrderId should not implicitly convert to Price");

    // Verify Sequence types are distinct
    static_assert(!std::is_convertible_v<Sequence, TradeSequence>,
                  "Sequence should not convert to TradeSequence");
    static_assert(!std::is_convertible_v<TradeSequence, Sequence>,
                  "TradeSequence should not convert to Sequence");
}

TEST(CoreTypesTest, OrderIdConstruction) {
    OrderId id1{42};
    OrderId id2{42};
    OrderId id3{99};

    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, id3);
    EXPECT_EQ(id1.value, 42u);
}

TEST(CoreTypesTest, PriceComparison) {
    Price p1{10000};
    Price p2{10020};
    Price p3{10000};

    EXPECT_LT(p1, p2);
    EXPECT_GT(p2, p1);
    EXPECT_EQ(p1, p3);
    EXPECT_LE(p1, p3);
    EXPECT_GE(p1, p3);
}

TEST(CoreTypesTest, QuantityArithmetic) {
    Quantity q1{100};
    Quantity q2{30};

    Quantity sum = q1 + q2;
    EXPECT_EQ(sum.value, 130u);

    Quantity diff = q1 - q2;
    EXPECT_EQ(diff.value, 70u);

    q1 += q2;
    EXPECT_EQ(q1.value, 130u);

    q1 -= q2;
    EXPECT_EQ(q1.value, 100u);
}

TEST(CoreTypesTest, SequenceIncrement) {
    Sequence seq{0};

    EXPECT_EQ(seq.value, 0u);
    ++seq;
    EXPECT_EQ(seq.value, 1u);
    seq++;
    EXPECT_EQ(seq.value, 2u);
}

TEST(CoreTypesTest, TradeSequenceIncrement) {
    TradeSequence trade_seq{0};

    EXPECT_EQ(trade_seq.value, 0u);
    ++trade_seq;
    EXPECT_EQ(trade_seq.value, 1u);
    trade_seq++;
    EXPECT_EQ(trade_seq.value, 2u);
}

TEST(CoreTypesTest, SideEnum) {
    Side buy = Side::Buy;
    Side sell = Side::Sell;

    EXPECT_NE(buy, sell);
    EXPECT_EQ(buy, Side::Buy);
    EXPECT_EQ(sell, Side::Sell);
}

TEST(CoreTypesTest, OrderIdHashable) {
    // Verify OrderId can be used in unordered containers
    std::unordered_set<OrderId> id_set;
    id_set.insert(OrderId{1});
    id_set.insert(OrderId{2});
    id_set.insert(OrderId{1});  // duplicate

    EXPECT_EQ(id_set.size(), 2u);
    EXPECT_TRUE(id_set.contains(OrderId{1}));
    EXPECT_TRUE(id_set.contains(OrderId{2}));
    EXPECT_FALSE(id_set.contains(OrderId{3}));
}
