#include "orderbook/price_level.hpp"

#include <gtest/gtest.h>

namespace miniexchange {
namespace {

// Helper to create an Order with given id, quantity, and sequence.
// Side and price are set to defaults since PriceLevel doesn't care about them
// (it only uses quantity for total_qty_ tracking).
Order make_order(uint64_t id, uint64_t qty, uint64_t seq) {
    Order o{
        OrderId{id},
        Side::Buy,
        Price{10000},
        Quantity{qty},
        Sequence{seq},
        nullptr,
        nullptr,
        nullptr
    };
    return o;
}

// --- Test: Construction ---

TEST(PriceLevelTest, ConstructionSetsPrice) {
    PriceLevel level{Price{10050}};
    EXPECT_EQ(level.price(), Price{10050});
}

TEST(PriceLevelTest, NewLevelIsEmpty) {
    PriceLevel level{Price{10050}};
    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.front(), nullptr);
    EXPECT_EQ(level.total_quantity(), Quantity{0});
}

// --- Test: push_back and FIFO ordering ---

TEST(PriceLevelTest, PushBackSingleOrder) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);

    level.push_back(&o1);

    EXPECT_FALSE(level.empty());
    EXPECT_EQ(level.front(), &o1);
    EXPECT_EQ(level.total_quantity(), Quantity{100});
    EXPECT_EQ(o1.level, &level);
    EXPECT_EQ(o1.prev, nullptr);
    EXPECT_EQ(o1.next, nullptr);
}

TEST(PriceLevelTest, PushBackThreeOrdersFIFO) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);
    Order o2 = make_order(2, 200, 2);
    Order o3 = make_order(3, 300, 3);

    level.push_back(&o1);
    level.push_back(&o2);
    level.push_back(&o3);

    // FIFO: front is o1 (first in), then o2, then o3
    EXPECT_EQ(level.front(), &o1);
    EXPECT_EQ(o1.next, &o2);
    EXPECT_EQ(o2.next, &o3);
    EXPECT_EQ(o3.next, nullptr);

    // Reverse links
    EXPECT_EQ(o3.prev, &o2);
    EXPECT_EQ(o2.prev, &o1);
    EXPECT_EQ(o1.prev, nullptr);

    // All orders point back to this level
    EXPECT_EQ(o1.level, &level);
    EXPECT_EQ(o2.level, &level);
    EXPECT_EQ(o3.level, &level);

    // Total quantity is sum of all
    EXPECT_EQ(level.total_quantity(), Quantity{600});
}

// --- Test: remove middle order ---

TEST(PriceLevelTest, RemoveMiddleOrder) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);
    Order o2 = make_order(2, 200, 2);
    Order o3 = make_order(3, 300, 3);

    level.push_back(&o1);
    level.push_back(&o2);
    level.push_back(&o3);

    level.remove(&o2);

    // o1 and o3 are now linked directly
    EXPECT_EQ(level.front(), &o1);
    EXPECT_EQ(o1.next, &o3);
    EXPECT_EQ(o3.prev, &o1);
    EXPECT_EQ(o1.prev, nullptr);
    EXPECT_EQ(o3.next, nullptr);

    // Removed order's pointers are cleared
    EXPECT_EQ(o2.prev, nullptr);
    EXPECT_EQ(o2.next, nullptr);
    EXPECT_EQ(o2.level, nullptr);

    // Total quantity reflects removal
    EXPECT_EQ(level.total_quantity(), Quantity{400});
    EXPECT_FALSE(level.empty());
}

// --- Test: remove head order ---

TEST(PriceLevelTest, RemoveHeadOrder) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);
    Order o2 = make_order(2, 200, 2);
    Order o3 = make_order(3, 300, 3);

    level.push_back(&o1);
    level.push_back(&o2);
    level.push_back(&o3);

    level.remove(&o1);

    // o2 is now the new head
    EXPECT_EQ(level.front(), &o2);
    EXPECT_EQ(o2.prev, nullptr);
    EXPECT_EQ(o2.next, &o3);
    EXPECT_EQ(o3.prev, &o2);

    // Removed order's pointers are cleared
    EXPECT_EQ(o1.prev, nullptr);
    EXPECT_EQ(o1.next, nullptr);
    EXPECT_EQ(o1.level, nullptr);

    EXPECT_EQ(level.total_quantity(), Quantity{500});
}

// --- Test: remove tail order ---

TEST(PriceLevelTest, RemoveTailOrder) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);
    Order o2 = make_order(2, 200, 2);
    Order o3 = make_order(3, 300, 3);

    level.push_back(&o1);
    level.push_back(&o2);
    level.push_back(&o3);

    level.remove(&o3);

    // o2 is now the tail
    EXPECT_EQ(level.front(), &o1);
    EXPECT_EQ(o1.next, &o2);
    EXPECT_EQ(o2.next, nullptr);
    EXPECT_EQ(o2.prev, &o1);

    // Removed order's pointers are cleared
    EXPECT_EQ(o3.prev, nullptr);
    EXPECT_EQ(o3.next, nullptr);
    EXPECT_EQ(o3.level, nullptr);

    EXPECT_EQ(level.total_quantity(), Quantity{300});
}

// --- Test: remove the only order makes level empty ---

TEST(PriceLevelTest, RemoveOnlyOrderMakesEmpty) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 50, 1);

    level.push_back(&o1);
    EXPECT_FALSE(level.empty());

    level.remove(&o1);

    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.front(), nullptr);
    EXPECT_EQ(level.total_quantity(), Quantity{0});
    EXPECT_EQ(o1.level, nullptr);
}

// --- Test: remove all orders one by one ---

TEST(PriceLevelTest, RemoveAllOrdersSequentially) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 10, 1);
    Order o2 = make_order(2, 20, 2);
    Order o3 = make_order(3, 30, 3);

    level.push_back(&o1);
    level.push_back(&o2);
    level.push_back(&o3);

    level.remove(&o1);
    EXPECT_EQ(level.front(), &o2);
    EXPECT_EQ(level.total_quantity(), Quantity{50});

    level.remove(&o3);
    EXPECT_EQ(level.front(), &o2);
    EXPECT_EQ(o2.prev, nullptr);
    EXPECT_EQ(o2.next, nullptr);
    EXPECT_EQ(level.total_quantity(), Quantity{20});

    level.remove(&o2);
    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.front(), nullptr);
    EXPECT_EQ(level.total_quantity(), Quantity{0});
}

// --- Test: total_quantity is O(1) (code-review check) ---
// This is a structural guarantee, not something measurable in a unit test.
// The implementation stores total_qty_ as a member, updated incrementally
// on push_back (+= qty) and remove (-= qty). The getter just returns it.
// Code review confirms: no loop, no traversal, no allocation in
// total_quantity().

TEST(PriceLevelTest, TotalQuantityIsIncrementallyMaintained) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);
    Order o2 = make_order(2, 250, 2);

    level.push_back(&o1);
    EXPECT_EQ(level.total_quantity(), Quantity{100});

    level.push_back(&o2);
    EXPECT_EQ(level.total_quantity(), Quantity{350});

    level.remove(&o1);
    EXPECT_EQ(level.total_quantity(), Quantity{250});

    level.remove(&o2);
    EXPECT_EQ(level.total_quantity(), Quantity{0});
}

// --- Test: push after remove works correctly ---

TEST(PriceLevelTest, PushAfterRemoveWorks) {
    PriceLevel level{Price{10050}};
    Order o1 = make_order(1, 100, 1);
    Order o2 = make_order(2, 200, 2);

    level.push_back(&o1);
    level.remove(&o1);
    EXPECT_TRUE(level.empty());

    // Push a new order after emptying the level
    level.push_back(&o2);
    EXPECT_FALSE(level.empty());
    EXPECT_EQ(level.front(), &o2);
    EXPECT_EQ(o2.prev, nullptr);
    EXPECT_EQ(o2.next, nullptr);
    EXPECT_EQ(o2.level, &level);
    EXPECT_EQ(level.total_quantity(), Quantity{200});
}

}  // namespace
}  // namespace miniexchange
