#include "lockfree_queue/spsc_ring_buffer.hpp"
#include "core/EngineCommand.hpp"

#include <gtest/gtest.h>
#include <cstdint>

namespace miniexchange {
namespace {

// Use a small capacity for deterministic, fast tests.
using SmallBuffer = SpscRingBuffer<int, 4>;

TEST(SpscRingBufferTest, EmptyOnConstruction) {
    SmallBuffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

TEST(SpscRingBufferTest, TryPopOnEmptyReturnsFalse) {
    SmallBuffer buf;
    int out = -1;
    EXPECT_FALSE(buf.try_pop(out));
    EXPECT_EQ(out, -1);  // unchanged
}

TEST(SpscRingBufferTest, PushUpToCapacityThenFull) {
    SmallBuffer buf;
    EXPECT_TRUE(buf.try_push(10));
    EXPECT_TRUE(buf.try_push(20));
    EXPECT_TRUE(buf.try_push(30));
    EXPECT_TRUE(buf.try_push(40));
    // Buffer is now full (4 items, capacity 4)
    EXPECT_FALSE(buf.try_push(50));
    EXPECT_EQ(buf.size(), 4u);
}

TEST(SpscRingBufferTest, FifoOrder) {
    SmallBuffer buf;
    buf.try_push(1);
    buf.try_push(2);
    buf.try_push(3);
    buf.try_push(4);

    int out = 0;
    EXPECT_TRUE(buf.try_pop(out));
    EXPECT_EQ(out, 1);
    EXPECT_TRUE(buf.try_pop(out));
    EXPECT_EQ(out, 2);
    EXPECT_TRUE(buf.try_pop(out));
    EXPECT_EQ(out, 3);
    EXPECT_TRUE(buf.try_pop(out));
    EXPECT_EQ(out, 4);
    EXPECT_FALSE(buf.try_pop(out));  // empty
}

TEST(SpscRingBufferTest, WrapAround) {
    // Push 4 (fill), pop 2, push 2 more — exercises wrap-around.
    SmallBuffer buf;
    buf.try_push(1);
    buf.try_push(2);
    buf.try_push(3);
    buf.try_push(4);

    int out = 0;
    buf.try_pop(out);  // pop 1
    buf.try_pop(out);  // pop 2

    EXPECT_TRUE(buf.try_push(5));  // wraps to slot 0
    EXPECT_TRUE(buf.try_push(6));  // wraps to slot 1

    // Should read 3, 4, 5, 6 in order
    buf.try_pop(out); EXPECT_EQ(out, 3);
    buf.try_pop(out); EXPECT_EQ(out, 4);
    buf.try_pop(out); EXPECT_EQ(out, 5);
    buf.try_pop(out); EXPECT_EQ(out, 6);
    EXPECT_TRUE(buf.empty());
}

TEST(SpscRingBufferTest, InterleavedPushPop) {
    SmallBuffer buf;
    int out = 0;

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(buf.try_push(i));
        ASSERT_TRUE(buf.try_pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_TRUE(buf.empty());
}

TEST(SpscRingBufferTest, CapacityAccessor) {
    EXPECT_EQ(SmallBuffer::capacity(), 4u);
    using BigBuffer = SpscRingBuffer<int, 1024>;
    EXPECT_EQ(BigBuffer::capacity(), 1024u);
}

TEST(SpscRingBufferTest, WorksWithEngineCommand) {
    // Verify the real payload type works correctly in the buffer.
    SpscRingBuffer<EngineCommand, 8> buf;

    LimitOrder lo{OrderId{1}, Side::Buy, Price{100}, Quantity{50}};
    MarketOrder mo{OrderId{2}, Side::Sell, Quantity{25}};
    CancelRequest cr{OrderId{3}};

    EXPECT_TRUE(buf.try_push(lo));
    EXPECT_TRUE(buf.try_push(mo));
    EXPECT_TRUE(buf.try_push(cr));
    EXPECT_EQ(buf.size(), 3u);

    EngineCommand out;
    EXPECT_TRUE(buf.try_pop(out));
    ASSERT_TRUE(std::holds_alternative<LimitOrder>(out));
    EXPECT_EQ(std::get<LimitOrder>(out).id, OrderId{1});

    EXPECT_TRUE(buf.try_pop(out));
    ASSERT_TRUE(std::holds_alternative<MarketOrder>(out));
    EXPECT_EQ(std::get<MarketOrder>(out).id, OrderId{2});

    EXPECT_TRUE(buf.try_pop(out));
    ASSERT_TRUE(std::holds_alternative<CancelRequest>(out));
    EXPECT_EQ(std::get<CancelRequest>(out).id, OrderId{3});

    EXPECT_TRUE(buf.empty());
}

TEST(SpscRingBufferTest, MoveSemantics) {
    // Verifies that try_push moves the item in and try_pop moves it out.
    // Use a type that's move-only to prove moves (not copies) happen.
    struct MoveOnly {
        int value;
        bool moved_from = false;
        MoveOnly() : value(0) {}
        explicit MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&& other) noexcept
            : value(other.value), moved_from(false) {
            other.moved_from = true;
        }
        MoveOnly& operator=(MoveOnly&& other) noexcept {
            value = other.value;
            moved_from = false;
            other.moved_from = true;
            return *this;
        }
    };

    SpscRingBuffer<MoveOnly, 4> buf;
    MoveOnly item(42);
    EXPECT_TRUE(buf.try_push(std::move(item)));
    EXPECT_TRUE(item.moved_from);

    MoveOnly out;
    EXPECT_TRUE(buf.try_pop(out));
    EXPECT_EQ(out.value, 42);
}

// Note: static_assert on non-power-of-two is verified by the fact that
// the following would fail to compile (tested manually, not in CI):
// SpscRingBuffer<int, 5> bad_buffer;  // static_assert fires

}  // namespace
}  // namespace miniexchange
