#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/TaggedCommand.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

using namespace miniexchange;

// ============================================================
// R8 spin-retry path test: when the outbound queue is full, the
// engine thread spins (try_push in a loop) until the I/O consumer
// frees a slot. This test exercises that path deterministically
// with a deliberately small queue (capacity 4).
// ============================================================

TEST(R8SpinTest, SpinSucceedsOnceSpaceFreed) {
    // Deliberately small queue to force the spin path
    SpscRingBuffer<TaggedResponse, 4> queue;

    // Fill queue to capacity
    for (int i = 0; i < 4; ++i) {
        TaggedResponse resp;
        resp.client = ClientId{static_cast<uint64_t>(i)};
        resp.response.status = EngineResult::Accepted;
        resp.response.remaining_qty = Quantity{0};
        ASSERT_TRUE(queue.try_push(std::move(resp)));
    }

    // Queue is now full — next try_push would fail
    TaggedResponse extra;
    extra.client = ClientId{99};
    extra.response.status = EngineResult::Accepted;
    extra.response.remaining_qty = Quantity{42};
    ASSERT_FALSE(queue.try_push(TaggedResponse{extra}));  // confirm full

    // Spin-push in a producer thread (mirrors engine thread behavior)
    std::atomic<bool> push_completed{false};
    std::thread producer([&]() {
        // R8 spin-retry pattern: loop until push succeeds
        while (!queue.try_push(TaggedResponse{extra})) {
            // spin (in real code: pause instruction or yield)
        }
        push_completed.store(true, std::memory_order_release);
    });

    // Brief delay to ensure producer is actually spinning
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(push_completed.load(std::memory_order_acquire));  // still spinning

    // Consumer frees a slot (simulates I/O thread draining the queue)
    TaggedResponse popped;
    ASSERT_TRUE(queue.try_pop(popped));

    // Wait for producer to complete (should be nearly instant once space freed)
    producer.join();

    EXPECT_TRUE(push_completed.load(std::memory_order_acquire));

    // Verify the pushed item is in the queue: we popped 1 of the original
    // 4, leaving 3 originals + 1 newly pushed = 4 items total remaining
    int count = 0;
    while (queue.try_pop(popped)) {
        ++count;
    }
    EXPECT_EQ(count, 4);  // 3 remaining originals + the extra we spin-pushed
}
