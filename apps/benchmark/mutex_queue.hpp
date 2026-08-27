#ifndef MINIEXCHANGE_APPS_BENCHMARK_MUTEX_QUEUE_HPP
#define MINIEXCHANGE_APPS_BENCHMARK_MUTEX_QUEUE_HPP

#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace miniexchange {

// MutexQueue — a mutex-guarded baseline queue with the same try_push/
// try_pop non-blocking-poll signature as SpscRingBuffer. Used exclusively
// for benchmark comparison (design.md §4) — lives in apps/benchmark/
// since it's a throwaway comparison target, not part of the shipped
// architecture.
//
// Key design choice: no condition_variable wait — both try_push and
// try_pop acquire the lock and return immediately (with true/false).
// This isolates "lock overhead" from "blocking vs. polling" as separate
// performance variables, making the comparison with SpscRingBuffer fair.
template <typename T, std::size_t Capacity = 4096>
class MutexQueue {
public:
    bool try_push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= Capacity) {
            return false;  // full — same reject semantics as ring buffer
        }
        queue_.push_back(std::move(item));
        return true;
    }

    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    static constexpr std::size_t capacity() { return Capacity; }

private:
    mutable std::mutex mutex_;
    std::deque<T> queue_;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_APPS_BENCHMARK_MUTEX_QUEUE_HPP
