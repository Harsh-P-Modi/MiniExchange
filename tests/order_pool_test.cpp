#include "orderbook/order_pool.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <vector>

#include <gtest/gtest.h>

namespace miniexchange {
namespace {

// --- Basic acquire/release tests ---

TEST(OrderPoolTest, AcquireReturnsDistinctPointersUpToCapacity) {
    constexpr std::size_t kCapacity = 64;
    OrderPool pool(kCapacity);

    std::set<Order*> acquired;
    for (std::size_t i = 0; i < kCapacity; ++i) {
        Order* order = pool.acquire();
        ASSERT_NE(order, nullptr) << "acquire() returned nullptr at iteration " << i;
        auto [it, inserted] = acquired.insert(order);
        EXPECT_TRUE(inserted) << "acquire() returned a duplicate pointer at iteration " << i;
    }
    EXPECT_EQ(acquired.size(), kCapacity);
}

TEST(OrderPoolTest, AcquirePastCapacityReturnsNullptr) {
    constexpr std::size_t kCapacity = 4;
    OrderPool pool(kCapacity);

    // Exhaust the pool.
    for (std::size_t i = 0; i < kCapacity; ++i) {
        ASSERT_NE(pool.acquire(), nullptr);
    }

    // Next acquire must return nullptr.
    EXPECT_EQ(pool.acquire(), nullptr);
    // Repeated attempts also return nullptr.
    EXPECT_EQ(pool.acquire(), nullptr);
}

TEST(OrderPoolTest, ReleaseThenAcquireReturnsSameAddress) {
    constexpr std::size_t kCapacity = 8;
    OrderPool pool(kCapacity);

    Order* first = pool.acquire();
    ASSERT_NE(first, nullptr);

    pool.release(first);

    // The very next acquire should recycle the just-released slot.
    Order* recycled = pool.acquire();
    EXPECT_EQ(recycled, first);
}

TEST(OrderPoolTest, AvailableReflectsAcquireAndRelease) {
    constexpr std::size_t kCapacity = 16;
    OrderPool pool(kCapacity);

    EXPECT_EQ(pool.available(), kCapacity);
    EXPECT_EQ(pool.capacity(), kCapacity);

    // Acquire some slots.
    std::vector<Order*> orders;
    for (std::size_t i = 0; i < 5; ++i) {
        orders.push_back(pool.acquire());
    }
    EXPECT_EQ(pool.available(), kCapacity - 5);

    // Release two.
    pool.release(orders[0]);
    pool.release(orders[2]);
    EXPECT_EQ(pool.available(), kCapacity - 3);

    // Acquire one more.
    orders.push_back(pool.acquire());
    EXPECT_EQ(pool.available(), kCapacity - 4);
}

TEST(OrderPoolTest, AllPointersWithinStorageRange) {
    constexpr std::size_t kCapacity = 32;
    OrderPool pool(kCapacity);

    // Acquire the first pointer to get the base address.
    Order* first = pool.acquire();
    ASSERT_NE(first, nullptr);

    std::vector<Order*> orders;
    orders.push_back(first);

    for (std::size_t i = 1; i < kCapacity; ++i) {
        orders.push_back(pool.acquire());
    }

    // All pointers must be within [first, first + capacity).
    Order* min_ptr = *std::min_element(orders.begin(), orders.end());
    Order* max_ptr = *std::max_element(orders.begin(), orders.end());

    // The span from min to max must be exactly (capacity - 1) Order slots.
    std::ptrdiff_t span = max_ptr - min_ptr;
    EXPECT_EQ(static_cast<std::size_t>(span), kCapacity - 1);

    // Every pointer must be at an integral offset from min_ptr.
    for (Order* o : orders) {
        std::ptrdiff_t offset = o - min_ptr;
        EXPECT_GE(offset, 0);
        EXPECT_LT(static_cast<std::size_t>(offset), kCapacity);
    }
}

// --- Recycling behavior tests ---

TEST(OrderPoolTest, FreeListRecyclesInLIFOOrder) {
    constexpr std::size_t kCapacity = 4;
    OrderPool pool(kCapacity);

    Order* a = pool.acquire();
    Order* b = pool.acquire();
    Order* c = pool.acquire();

    // Release in order: c, b, a -> free list becomes a -> b -> c
    pool.release(c);
    pool.release(b);
    pool.release(a);

    // Acquire should return in LIFO order: a, b, c
    EXPECT_EQ(pool.acquire(), a);
    EXPECT_EQ(pool.acquire(), b);
    EXPECT_EQ(pool.acquire(), c);
}

TEST(OrderPoolTest, ExhaustAndFullyRecycle) {
    constexpr std::size_t kCapacity = 8;
    OrderPool pool(kCapacity);

    // Exhaust the pool.
    std::vector<Order*> orders;
    for (std::size_t i = 0; i < kCapacity; ++i) {
        orders.push_back(pool.acquire());
    }
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.acquire(), nullptr);

    // Release all.
    for (Order* o : orders) {
        pool.release(o);
    }
    EXPECT_EQ(pool.available(), kCapacity);

    // Re-acquire all — should succeed without nullptr.
    for (std::size_t i = 0; i < kCapacity; ++i) {
        EXPECT_NE(pool.acquire(), nullptr);
    }
    EXPECT_EQ(pool.available(), 0u);
}

// --- Edge cases ---

TEST(OrderPoolTest, ZeroCapacityPoolAlwaysReturnsNullptr) {
    OrderPool pool(0);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.acquire(), nullptr);
}

TEST(OrderPoolTest, SingleSlotPool) {
    OrderPool pool(1);
    EXPECT_EQ(pool.available(), 1u);

    Order* only = pool.acquire();
    ASSERT_NE(only, nullptr);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.acquire(), nullptr);

    pool.release(only);
    EXPECT_EQ(pool.available(), 1u);
    EXPECT_EQ(pool.acquire(), only);
}

// --- Debug assertion test ---

#ifdef NDEBUG
// In release builds this test is skipped (assertion is compiled out).
TEST(OrderPoolTest, DISABLED_ReleaseOutOfRangeAssertsInDebug) {
    // This test only makes sense in debug builds where assert() fires.
}
#else
TEST(OrderPoolDeathTest, ReleaseOutOfRangeAssertsInDebug) {
    constexpr std::size_t kCapacity = 4;
    OrderPool pool(kCapacity);

    // Manufacture a pointer that's outside the pool's storage range.
    Order bogus{OrderId{0}, Side::Buy, Price{100}, Quantity{10}, Sequence{0},
                nullptr, nullptr, nullptr};
    EXPECT_DEATH(pool.release(&bogus), "");
}
#endif

}  // namespace
}  // namespace miniexchange
