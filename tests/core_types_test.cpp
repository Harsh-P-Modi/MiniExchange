#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <type_traits>
#include <unordered_map>
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
    EXPECT_TRUE(id_set.count(OrderId{1}) > 0);
    EXPECT_TRUE(id_set.count(OrderId{2}) > 0);
    EXPECT_FALSE(id_set.count(OrderId{3}) > 0);
}

// --- ClientId Tests ---

TEST(ClientIdTest, NoImplicitConstruction) {
    // ClientId's constructor is explicit — cannot implicitly construct from uint64_t
    static_assert(!std::is_convertible_v<uint64_t, ClientId>,
                  "ClientId should not be implicitly constructible from uint64_t");
}

TEST(ClientIdTest, NoImplicitConversionToUint64) {
    // ClientId should not implicitly convert back to uint64_t
    static_assert(!std::is_convertible_v<ClientId, uint64_t>,
                  "ClientId should not implicitly convert to uint64_t");
}

TEST(ClientIdTest, EqualityAndInequality) {
    ClientId c1{100};
    ClientId c2{100};
    ClientId c3{200};

    EXPECT_EQ(c1, c2);
    EXPECT_NE(c1, c3);
    EXPECT_EQ(c1.value, 100u);
    EXPECT_EQ(c3.value, 200u);
}

TEST(ClientIdTest, HashConsistency) {
    std::hash<ClientId> hasher;

    ClientId c1{42};
    ClientId c2{42};
    ClientId c3{99};

    // Same value → same hash
    EXPECT_EQ(hasher(c1), hasher(c2));

    // Different values → different hashes (not guaranteed in general,
    // but for trivial uint64_t-based hashes this should hold for small inputs)
    EXPECT_NE(hasher(c1), hasher(c3));
}

TEST(ClientIdTest, UsableAsUnorderedMapKey) {
    // Compile-time verification: unordered_map<ClientId, int> must compile
    std::unordered_map<ClientId, int> client_map;
    client_map[ClientId{1}] = 10;
    client_map[ClientId{2}] = 20;
    client_map[ClientId{1}] = 30;  // overwrite

    EXPECT_EQ(client_map.size(), 2u);
    EXPECT_EQ(client_map[ClientId{1}], 30);
    EXPECT_EQ(client_map[ClientId{2}], 20);
}

TEST(ClientIdTest, UsableInUnorderedSet) {
    std::unordered_set<ClientId> client_set;
    client_set.insert(ClientId{5});
    client_set.insert(ClientId{10});
    client_set.insert(ClientId{5});  // duplicate

    EXPECT_EQ(client_set.size(), 2u);
    EXPECT_TRUE(client_set.count(ClientId{5}) > 0);
    EXPECT_TRUE(client_set.count(ClientId{10}) > 0);
    EXPECT_FALSE(client_set.count(ClientId{99}) > 0);
}

TEST(ClientIdTest, DistinctFromOrderId) {
    // ClientId and OrderId are different types — no implicit conversion
    static_assert(!std::is_convertible_v<ClientId, OrderId>,
                  "ClientId should not implicitly convert to OrderId");
    static_assert(!std::is_convertible_v<OrderId, ClientId>,
                  "OrderId should not implicitly convert to ClientId");
}
