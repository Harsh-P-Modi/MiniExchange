#include "lockfree_queue/spsc_ring_buffer.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace miniexchange {
namespace {

// Stress test: one producer thread pushes a known sequence (0..N-1),
// one consumer thread pops and records what it received. Then we assert:
//   1. Nothing lost (received count == sent count)
//   2. Nothing duplicated (each value appears exactly once)
//   3. Nothing reordered (values arrive in strictly ascending order)
//
// Per requirements.md NFR3: run under ThreadSanitizer to detect data
// races. The test itself is deterministic given correct SPSC behavior.

static constexpr std::size_t kItemCount = 2'000'000;
static constexpr std::size_t kBufferCapacity = 4096;

TEST(SpscStressTest, ProducerConsumerCorrectness) {
    SpscRingBuffer<uint64_t, kBufferCapacity> buffer;

    std::atomic<bool> producer_done{false};
    std::vector<uint64_t> received;
    received.reserve(kItemCount);

    // Producer thread: push 0..kItemCount-1 as fast as try_push allows.
    // On full buffer, spin-retry (acceptable for a stress test — in
    // production, the adapter would reject or back-pressure).
    std::thread producer([&]() {
        for (uint64_t i = 0; i < kItemCount; ++i) {
            while (!buffer.try_push(i)) {
                // spin — buffer is full, wait for consumer to drain
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer thread: pop until producer is done AND buffer is empty.
    std::thread consumer([&]() {
        uint64_t val = 0;
        while (true) {
            if (buffer.try_pop(val)) {
                received.push_back(val);
            } else if (producer_done.load(std::memory_order_acquire)) {
                // Drain any remaining items after producer signals done
                while (buffer.try_pop(val)) {
                    received.push_back(val);
                }
                break;
            }
            // else: empty but producer not done — spin
        }
    });

    producer.join();
    consumer.join();

    // Assertion 1: nothing lost
    ASSERT_EQ(received.size(), kItemCount)
        << "Lost items: expected " << kItemCount
        << ", got " << received.size();

    // Assertion 3: nothing reordered (implies no duplicates either,
    // since a strictly ascending sequence has all unique values)
    for (std::size_t i = 0; i < received.size(); ++i) {
        ASSERT_EQ(received[i], i)
            << "Reorder or duplication at index " << i
            << ": expected " << i << ", got " << received[i];
    }
}

// Coarse throughput floor: verify the queue isn't accidentally
// serializing somewhere. >10M ops/sec is a low bar that any correct
// lock-free implementation should clear on modern hardware.
TEST(SpscStressTest, ThroughputFloor) {
    SpscRingBuffer<uint64_t, kBufferCapacity> buffer;

    static constexpr std::size_t kOps = 1'000'000;
    std::atomic<bool> producer_done{false};

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        for (uint64_t i = 0; i < kOps; ++i) {
            while (!buffer.try_push(i)) {}
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        uint64_t val = 0;
        std::size_t count = 0;
        while (true) {
            if (buffer.try_pop(val)) {
                ++count;
            } else if (producer_done.load(std::memory_order_acquire)) {
                while (buffer.try_pop(val)) { ++count; }
                break;
            }
        }
        EXPECT_EQ(count, kOps);
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          end - start)
                          .count();
    double ops_per_sec = static_cast<double>(kOps) /
                         (static_cast<double>(elapsed_ns) / 1e9);

    // Low bar: 10M ops/sec. Any correct SPSC ring buffer should easily
    // exceed this on modern hardware. If this fails, something is
    // accidentally serializing (e.g., seq_cst everywhere, or a hidden
    // mutex).
    EXPECT_GT(ops_per_sec, 10'000'000.0)
        << "Throughput too low: " << ops_per_sec << " ops/sec";
}

// Test with EngineCommand payload (realistic size, ~40 bytes per slot)
TEST(SpscStressTest, EngineCommandPayload) {
    // Use uint64_t for the ordering test above; this test just confirms
    // larger payload types work under concurrency without data corruption.
    SpscRingBuffer<std::pair<uint64_t, uint64_t>, kBufferCapacity> buffer;

    static constexpr std::size_t kOps = 500'000;
    std::atomic<bool> done{false};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < kOps; ++i) {
            auto item = std::make_pair(i, i * 2);
            while (!buffer.try_push(item)) {}
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        std::pair<uint64_t, uint64_t> val;
        uint64_t expected = 0;
        while (true) {
            if (buffer.try_pop(val)) {
                ASSERT_EQ(val.first, expected);
                ASSERT_EQ(val.second, expected * 2);
                ++expected;
            } else if (done.load(std::memory_order_acquire)) {
                while (buffer.try_pop(val)) {
                    ASSERT_EQ(val.first, expected);
                    ASSERT_EQ(val.second, expected * 2);
                    ++expected;
                }
                break;
            }
        }
        EXPECT_EQ(expected, kOps);
    });

    producer.join();
    consumer.join();
}

}  // namespace
}  // namespace miniexchange
