# Phase 4 — Queue Comparison: Lock-Free vs. Mutex Baseline

**Environment:** Windows laptop, no CPU pinning, RelWithDebInfo build, 4096-slot queue capacity

## Isolated Per-Operation Latency (single-threaded)

| Operation | Queue | Avg (ns) | Median (ns) | P99 (ns) | Max (ns) |
|---|---|---|---|---|---|
| try_push | SpscRingBuffer | 82.2 | 100.0 | 100.0 | 77400.0 |
| try_push | MutexQueue | 124.4 | 100.0 | 200.0 | 57800.0 |
| try_pop | SpscRingBuffer | 79.8 | 100.0 | 100.0 | 52700.0 |
| try_pop | MutexQueue | 114.1 | 100.0 | 200.0 | 60300.0 |

## Two-Thread Producer/Consumer Throughput

| Queue | Throughput (ops/sec) |
|---|---|
| SpscRingBuffer | 36690246 |
| MutexQueue | 5117410 |

**Speedup:** SpscRingBuffer is 7.17x faster than MutexQueue in two-thread throughput.

## Interpretation

- **Isolated latency (single-threaded):** With no contention, the mutex has minimal overhead (no actual blocking occurs). The lock-free buffer may show similar or slightly better numbers due to avoiding the mutex syscall overhead entirely — even an uncontended mutex requires an atomic compare-and-swap on Linux (futex fast path) or a kernel transition on Windows.
- **Two-thread throughput:** This is where the lock-free buffer should clearly outperform. Under sustained producer/consumer load, the mutex forces serialization (one thread waits while the other holds the lock), while the ring buffer allows both threads to progress simultaneously — the producer writes to tail without observing head's cache line (until checking if full), and the consumer reads from head without touching tail's cache line (until checking if empty).
- **Tail latency (P99/max):** The mutex's worst-case is unbounded under contention (thread can be descheduled while holding the lock, blocking the other indefinitely). The ring buffer's worst case is bounded by the try_push/try_pop operation itself (a few cache misses at most). This difference matters most under load — exactly Phase 5's scenario.
