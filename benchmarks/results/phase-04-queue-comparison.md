# Phase 4 — Queue Comparison: Lock-Free vs. Mutex Baseline

**Environment:** Linux (Ubuntu 24.04.5 LTS), Intel Core i5-13500H, kernel
7.0.0-31-generic, RelWithDebInfo, `taskset -c 2,3`, governor=performance,
turbo=off, 4096-slot queue capacity. Commit 164e03b.

Workload: 100,000 ops for the latency sweep, 2,000,000 ops for throughput
(5 repetitions, best-of).

## Isolated Per-Operation Latency (single-threaded)

| Operation | Queue | Median (ns) | P99 (ns) |
|---|---|---|---|
| try_push | SpscRingBuffer | 28 | 37 |
| try_push | MutexQueue | 28 | 63 |
| try_pop | SpscRingBuffer | 22 | 26 |
| try_pop | MutexQueue | 26 | 43 |

With no contention the two are near-identical at the median (the mutex's
fast path is a single uncontended atomic). The difference already shows
at P99: the mutex's push tail is ~1.7× the ring buffer's (63 vs 37 ns)
and its pop tail ~1.65× (43 vs 26 ns), because even the uncontended
`lock`/`unlock` pair has more work and more branch/cache surface than the
ring buffer's single acquire-load + release-store.

## Two-Thread Producer/Consumer Throughput

| Queue | Throughput (ops/sec) |
|---|---|
| SpscRingBuffer | 59,895,400 |
| MutexQueue | 4,502,230 |

**Speedup:** SpscRingBuffer is **13.30×** faster than MutexQueue in
two-thread throughput.

## Interpretation

- **Isolated latency (single-threaded):** With no contention the mutex has
  minimal overhead — no blocking occurs, so it is just an uncontended
  atomic CAS on the futex fast path. Medians are within a few ns. The
  lock-free buffer's advantage at the median is small; its real advantage
  is in the tail (P99 above) and under contention (below).
- **Two-thread throughput:** This is where the lock-free buffer clearly
  wins — 13×. Under sustained producer/consumer load the mutex serialises
  the two threads (one waits while the other holds the lock, plus the
  futex wake/wait syscalls once the uncontended fast path stops applying),
  while the ring buffer lets both progress simultaneously: the producer
  writes `tail` without touching `head`'s cache line until it needs to
  check "full", and the consumer reads `head` without touching `tail`'s
  cache line until it needs to check "empty".
- **Tail latency:** The mutex's worst case is unbounded under contention
  (a thread can be descheduled while holding the lock, stalling the
  other). The ring buffer's worst case is bounded by the try_push/try_pop
  operation itself — a few cache misses at most. This is exactly why the
  gateway (Phase 5) puts an SPSC ring on the order→ack critical path.
