#ifndef MINIEXCHANGE_LOCKFREE_QUEUE_SPSC_RING_BUFFER_HPP
#define MINIEXCHANGE_LOCKFREE_QUEUE_SPSC_RING_BUFFER_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace miniexchange {

// SpscRingBuffer — a fixed-capacity, lock-free, single-producer /
// single-consumer ring buffer.
//
// Design rationale (see design.md §3 and §6):
// - Capacity must be a power of two (enforced at compile time via
//   static_assert) so that index masking (& mask_) replaces modulo.
// - Head and tail indices are cache-line-padded to eliminate false
//   sharing between producer and consumer threads (NFR2).
// - Memory ordering uses acquire/release — not seq_cst — because SPSC
//   means each index has exactly one writer: tail_ is written only by
//   the producer, head_ only by the consumer. Acquire on read,
//   release on write provides the happens-before guarantee that the
//   data written to storage_[] is visible to the reader after they
//   observe the updated index.
// - The class itself is cache-line-aligned (alignas(64)) to prevent
//   head_ from sharing a cache line with whatever precedes this object
//   in memory (stack frame, heap header, etc.).
//
// Template parameters:
//   T        — element type (must be move-constructible)
//   Capacity — number of slots; must be a power of two
template <typename T, std::size_t Capacity = 4096>
class alignas(64) SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(Capacity > 0, "Capacity must be greater than zero");

public:
    // try_push — enqueue an item. Returns false if the buffer is full
    // (back-pressure policy: reject immediately, per requirements.md R4).
    //
    // Memory ordering:
    //   1. Load head_ with acquire — sees consumer's latest progress
    //      (how many slots have been freed).
    //   2. Write item into storage_[tail & mask_] — plain (non-atomic)
    //      access, safe because no other thread writes this slot while
    //      it's between tail and head.
    //   3. Store tail_ with release — publishes both the updated index
    //      AND the data written in step 2 to the consumer's subsequent
    //      acquire-load of tail_.
    bool try_push(T item) {
        const std::size_t tail = tail_.value.load(std::memory_order_relaxed);
        const std::size_t head = head_.value.load(std::memory_order_acquire);

        if (tail - head >= Capacity) {
            return false;  // full
        }

        storage_[tail & mask_] = std::move(item);
        tail_.value.store(tail + 1, std::memory_order_release);
        return true;
    }

    // try_pop — dequeue an item. Returns false if the buffer is empty
    // (non-blocking poll, per requirements.md R3).
    //
    // Memory ordering:
    //   1. Load tail_ with acquire — sees producer's latest write and
    //      the data it published alongside the index update.
    //   2. Read storage_[head & mask_] — plain access, safe because
    //      the acquire above establishes happens-before with the
    //      producer's release-store that published this slot.
    //   3. Store head_ with release — tells the producer that this
    //      slot is now free for reuse.
    bool try_pop(T& out) {
        const std::size_t head = head_.value.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.value.load(std::memory_order_acquire);

        if (head >= tail) {
            return false;  // empty
        }

        out = std::move(storage_[head & mask_]);
        head_.value.store(head + 1, std::memory_order_release);
        return true;
    }

    // size — approximate snapshot of current queue depth. Diagnostic
    // only: the value can be stale the instant it's read (producer/
    // consumer may modify head_/tail_ concurrently). Fine for
    // reporting "average queue depth during a benchmark run"; not fine
    // as a precondition for push/pop decisions.
    std::size_t size() const {
        return tail_.value.load(std::memory_order_relaxed) -
               head_.value.load(std::memory_order_relaxed);
    }

    bool empty() const { return size() == 0; }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    // PaddedIndex — each atomic index gets its own 64-byte cache line.
    // Without this, the producer writing tail_ would invalidate the
    // consumer's cache line holding head_ (and vice versa), causing
    // false sharing that destroys the performance advantage of being
    // lock-free. 64 bytes is the cache line size on all x86-64 CPUs
    // and most ARM64 implementations.
    struct alignas(64) PaddedIndex {
        std::atomic<std::size_t> value{0};
    };

    PaddedIndex head_;                    // consumer's read cursor
    PaddedIndex tail_;                    // producer's write cursor
    std::array<T, Capacity> storage_{};   // ring buffer slots

    static constexpr std::size_t mask_ = Capacity - 1;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_LOCKFREE_QUEUE_SPSC_RING_BUFFER_HPP
