#include "orderbook/order_book.hpp"

#include <gtest/gtest.h>

namespace miniexchange {
namespace {

// Helper: create an Order value with the given fields.
Order make_order(uint64_t id, Side side, int64_t price,
                 uint64_t qty, uint64_t seq) {
    return Order{
        .id = OrderId{id}, .side = side, .price = Price{price},
        .quantity = Quantity{qty}, .sequence = Sequence{seq},
        // owner defaults to ClientId{0}.
        .prev = nullptr, .next = nullptr, .level = nullptr};
}

// --- best_bid tests ---

TEST(OrderBookTest, BestBidReturnsHighestPrice) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    book.add_order(make_order(2, Side::Buy, 101, 10, 1));
    book.add_order(make_order(3, Side::Buy, 102, 10, 2));

    PriceLevel* best = book.best_bid();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price(), Price{102});
}

TEST(OrderBookTest, BestBidReturnsNullptrWhenEmpty) {
    OrderBook book;
    EXPECT_EQ(book.best_bid(), nullptr);
}

// --- best_ask tests ---

TEST(OrderBookTest, BestAskReturnsLowestPrice) {
    OrderBook book;
    book.add_order(make_order(1, Side::Sell, 200, 10, 0));
    book.add_order(make_order(2, Side::Sell, 199, 10, 1));
    book.add_order(make_order(3, Side::Sell, 198, 10, 2));

    PriceLevel* best = book.best_ask();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price(), Price{198});
}

TEST(OrderBookTest, BestAskReturnsNullptrWhenEmpty) {
    OrderBook book;
    EXPECT_EQ(book.best_ask(), nullptr);
}

// --- remove_order and level pruning ---

TEST(OrderBookTest, RemoveBestBidUpdatesTopOfBook) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    book.add_order(make_order(2, Side::Buy, 101, 10, 1));
    book.add_order(make_order(3, Side::Buy, 102, 10, 2));

    // Remove the best bid (price 102).
    Order* best_order = book.best_bid()->front();
    ASSERT_NE(best_order, nullptr);
    EXPECT_EQ(best_order->price, Price{102});

    book.remove_order(best_order);

    // New best bid should be 101.
    PriceLevel* new_best = book.best_bid();
    ASSERT_NE(new_best, nullptr);
    EXPECT_EQ(new_best->price(), Price{101});
}

TEST(OrderBookTest, RemoveLastOrderAtLevelPrunesLevel) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    book.add_order(make_order(2, Side::Buy, 101, 10, 1));

    // Remove the only order at price 101.
    Order* order_at_101 = book.find_order(OrderId{2});
    ASSERT_NE(order_at_101, nullptr);
    book.remove_order(order_at_101);

    // Price level 101 should no longer exist; best bid is now 100.
    PriceLevel* best = book.best_bid();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price(), Price{100});
}

TEST(OrderBookTest, RemoveAllOrdersLeavesBidSideEmpty) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));

    Order* order = book.find_order(OrderId{1});
    book.remove_order(order);

    EXPECT_EQ(book.best_bid(), nullptr);
    EXPECT_EQ(book.order_count(), 0u);
}

// --- order_count tests ---

TEST(OrderBookTest, OrderCountIncrements) {
    OrderBook book;
    EXPECT_EQ(book.order_count(), 0u);

    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    EXPECT_EQ(book.order_count(), 1u);

    book.add_order(make_order(2, Side::Sell, 200, 10, 1));
    EXPECT_EQ(book.order_count(), 2u);

    book.add_order(make_order(3, Side::Buy, 101, 10, 2));
    EXPECT_EQ(book.order_count(), 3u);
}

TEST(OrderBookTest, OrderCountDecrements) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    book.add_order(make_order(2, Side::Buy, 101, 10, 1));
    book.add_order(make_order(3, Side::Sell, 200, 10, 2));
    EXPECT_EQ(book.order_count(), 3u);

    book.remove_order(book.find_order(OrderId{2}));
    EXPECT_EQ(book.order_count(), 2u);

    book.remove_order(book.find_order(OrderId{1}));
    EXPECT_EQ(book.order_count(), 1u);

    book.remove_order(book.find_order(OrderId{3}));
    EXPECT_EQ(book.order_count(), 0u);
}

// --- FIFO ordering within a price level ---

TEST(OrderBookTest, SamePriceFIFOOrdering) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    book.add_order(make_order(2, Side::Buy, 100, 20, 1));
    book.add_order(make_order(3, Side::Buy, 100, 30, 2));

    PriceLevel* level = book.best_bid();
    ASSERT_NE(level, nullptr);

    // First inserted order should be at the front.
    Order* front = level->front();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, OrderId{1});
    EXPECT_EQ(front->quantity, Quantity{10});

    // Second order is next.
    ASSERT_NE(front->next, nullptr);
    EXPECT_EQ(front->next->id, OrderId{2});
    EXPECT_EQ(front->next->quantity, Quantity{20});

    // Third order is last.
    ASSERT_NE(front->next->next, nullptr);
    EXPECT_EQ(front->next->next->id, OrderId{3});
    EXPECT_EQ(front->next->next->quantity, Quantity{30});
}

// --- find_order tests ---

TEST(OrderBookTest, FindOrderReturnsCorrectOrder) {
    OrderBook book;
    book.add_order(make_order(42, Side::Buy, 100, 10, 0));

    Order* found = book.find_order(OrderId{42});
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, OrderId{42});
    EXPECT_EQ(found->price, Price{100});
}

TEST(OrderBookTest, FindOrderReturnsNullptrForMissingId) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));

    EXPECT_EQ(book.find_order(OrderId{999}), nullptr);
}

TEST(OrderBookTest, FindOrderReturnsNullptrAfterRemoval) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));

    Order* order = book.find_order(OrderId{1});
    book.remove_order(order);

    EXPECT_EQ(book.find_order(OrderId{1}), nullptr);
}

// --- Mixed bid/ask operations ---

TEST(OrderBookTest, BidsAndAsksIndependent) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10, 0));
    book.add_order(make_order(2, Side::Buy, 101, 10, 1));
    book.add_order(make_order(3, Side::Sell, 200, 10, 2));
    book.add_order(make_order(4, Side::Sell, 199, 10, 3));

    ASSERT_NE(book.best_bid(), nullptr);
    EXPECT_EQ(book.best_bid()->price(), Price{101});

    ASSERT_NE(book.best_ask(), nullptr);
    EXPECT_EQ(book.best_ask()->price(), Price{199});

    // Removing a bid doesn't affect asks.
    book.remove_order(book.find_order(OrderId{2}));
    EXPECT_EQ(book.best_bid()->price(), Price{100});
    EXPECT_EQ(book.best_ask()->price(), Price{199});
}

TEST(OrderBookTest, RemoveMiddleOrderPreservesFIFO) {
    OrderBook book;
    book.add_order(make_order(1, Side::Sell, 150, 10, 0));
    book.add_order(make_order(2, Side::Sell, 150, 20, 1));
    book.add_order(make_order(3, Side::Sell, 150, 30, 2));

    // Remove the middle order (id=2).
    Order* middle = book.find_order(OrderId{2});
    book.remove_order(middle);

    // Remaining FIFO: id=1 (front), id=3 (back).
    PriceLevel* level = book.best_ask();
    ASSERT_NE(level, nullptr);
    Order* front = level->front();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, OrderId{1});
    ASSERT_NE(front->next, nullptr);
    EXPECT_EQ(front->next->id, OrderId{3});
    EXPECT_EQ(front->next->next, nullptr);
}

}  // namespace
}  // namespace miniexchange
