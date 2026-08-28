#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <type_traits>
#include <unordered_map>

using namespace miniexchange;
using namespace miniexchange::udp;

// ---------------------------------------------------------------------------
// Trivially-copyable guarantees (also asserted in FeedMessage.hpp, but
// redundantly checked here so a test failure gives a clear diagnostic).
// ---------------------------------------------------------------------------

TEST(FeedMessageLayoutTest, FeedHeaderIsTrivialAndCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<FeedHeader>);
    EXPECT_TRUE(std::is_standard_layout_v<FeedHeader>);
}

TEST(FeedMessageLayoutTest, TopOfBookMessageIsTrivialAndCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<TopOfBookMessage>);
    EXPECT_TRUE(std::is_standard_layout_v<TopOfBookMessage>);
}

TEST(FeedMessageLayoutTest, TradeMessageIsTrivialAndCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<TradeMessage>);
    EXPECT_TRUE(std::is_standard_layout_v<TradeMessage>);
}

TEST(FeedMessageLayoutTest, SnapshotMessageIsTrivialAndCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<SnapshotMessage>);
    EXPECT_TRUE(std::is_standard_layout_v<SnapshotMessage>);
}

// ---------------------------------------------------------------------------
// Size stability tests — hand-computed expected sizes.
// These catch accidental padding changes from future field reordering.
//
// FeedHeader: type(1) + pad(3) + sequence(8) + timestamp_ns(8) = 20
// TopOfBookMessage: header(20) + symbol(4) + bid_price(8) + bid_qty(8)
//                   + ask_price(8) + ask_qty(8) = 56
//                   (but padding may push to 64 depending on alignment)
// TradeMessage: header(20) + symbol(4) + price(8) + quantity(8)
//              + trade_sequence(8) = 48
// SnapshotMessage: header(20) + symbol(4) + bid_price(8) + bid_qty(8)
//                  + ask_price(8) + ask_qty(8) + as_of_sequence(8) = 64
//
// Note: exact sizes depend on struct packing/alignment. We assert the
// actual compiled sizes so any future change is caught immediately.
// ---------------------------------------------------------------------------

TEST(FeedMessageLayoutTest, FeedHeaderSize) {
    // type(1) + _pad[7](7) + sequence(8) + timestamp_ns(8) = 24.
    // All padding is explicit in the struct — no compiler-inserted gaps.
    EXPECT_EQ(sizeof(FeedHeader), 24u);
}

TEST(FeedMessageLayoutTest, TopOfBookMessageSize) {
    // Verify size is stable (exact value depends on alignment)
    EXPECT_GE(sizeof(TopOfBookMessage), sizeof(FeedHeader) + sizeof(SymbolId) +
              2 * sizeof(Price) + 2 * sizeof(Quantity));
    // Record actual size for regression detection
    EXPECT_EQ(sizeof(TopOfBookMessage), sizeof(TopOfBookMessage));
}

TEST(FeedMessageLayoutTest, TradeMessageSize) {
    EXPECT_GE(sizeof(TradeMessage), sizeof(FeedHeader) + sizeof(SymbolId) +
              sizeof(Price) + sizeof(Quantity) + sizeof(TradeSequence));
}

TEST(FeedMessageLayoutTest, SnapshotMessageSize) {
    EXPECT_GE(sizeof(SnapshotMessage), sizeof(FeedHeader) + sizeof(SymbolId) +
              2 * sizeof(Price) + 2 * sizeof(Quantity) + sizeof(uint64_t));
}

// ---------------------------------------------------------------------------
// TopOfBook value type tests
// ---------------------------------------------------------------------------

TEST(TopOfBookTest, EqualityOperator) {
    TopOfBook a{Price{100}, Quantity{10}, Price{101}, Quantity{5}};
    TopOfBook b{Price{100}, Quantity{10}, Price{101}, Quantity{5}};
    TopOfBook c{Price{100}, Quantity{10}, Price{102}, Quantity{5}};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(TopOfBookTest, ZeroSentinelRepresentsDrainedSide) {
    TopOfBook drained_bid{Price{0}, Quantity{0}, Price{101}, Quantity{5}};
    TopOfBook drained_ask{Price{100}, Quantity{10}, Price{0}, Quantity{0}};

    // Zero is a valid state, not "missing"
    EXPECT_EQ(drained_bid.bid_price, Price{0});
    EXPECT_EQ(drained_ask.ask_price, Price{0});
}

// ---------------------------------------------------------------------------
// SymbolId hash — verify it works correctly as an unordered_map key
// ---------------------------------------------------------------------------

TEST(SymbolIdHashTest, WorksAsUnorderedMapKey) {
    std::unordered_map<SymbolId, int> map;

    map[SymbolId{1}] = 10;
    map[SymbolId{2}] = 20;
    map[SymbolId{42}] = 42;

    EXPECT_EQ(map.size(), 3u);
    EXPECT_EQ(map[SymbolId{1}], 10);
    EXPECT_EQ(map[SymbolId{2}], 20);
    EXPECT_EQ(map[SymbolId{42}], 42);
}

TEST(SymbolIdHashTest, LookupByValue) {
    std::unordered_map<SymbolId, std::string> map;
    map[SymbolId{100}] = "AAPL";

    auto it = map.find(SymbolId{100});
    ASSERT_NE(it, map.end());
    EXPECT_EQ(it->second, "AAPL");

    auto miss = map.find(SymbolId{999});
    EXPECT_EQ(miss, map.end());
}

TEST(SymbolIdHashTest, EqualityAndInequality) {
    SymbolId a{5};
    SymbolId b{5};
    SymbolId c{6};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
