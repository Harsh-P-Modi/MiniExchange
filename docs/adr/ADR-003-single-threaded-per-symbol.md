# ADR-003: Single-Threaded Per Symbol

**Status:** Accepted  
**Date:** Phase 1 (documented Phase 5)

## Context

A matching engine must decide how concurrency maps onto its order
books. The fundamental question: how many threads touch a single
symbol's order book, and what synchronization is needed?

Real exchanges (CME Globex, NASDAQ, LSE) assign each instrument to
exactly one core. This eliminates lock contention on the critical
matching path and gives deterministic, reproducible behavior for a
given input sequence — essential for both correctness testing and
latency measurement.

MiniExchange currently handles a single symbol. The question still
matters because Phase 4 introduces a lock-free queue at the
network → engine boundary, and Phase 5 adds a TCP I/O thread. Where
does the threading boundary live?

## Decision

The matching engine processes all orders for a single symbol on
exactly one thread. No locks, mutexes, or atomic operations exist
inside `engine/`, `orderbook/`, or `core/`. Period.

Thread boundaries are handled externally:
- Phase 4's `SpscRingBuffer` carries commands from the network I/O
  thread to the engine thread (single-producer, single-consumer — no
  lock needed).
- Phase 5's `TcpServer` runs on a separate I/O thread; it never
  touches the order book directly.

The engine thread busy-spins on the inbound queue (no `yield` or
`sleep`), consistent with the "one instrument pinned to one dedicated
core" philosophy used in production HFT systems.

## Alternatives Considered

1. **Multi-threaded engine with per-order locks** — one mutex per
   `Order` object, allowing concurrent matching within a single book.
   Rejected: the matching algorithm is inherently sequential
   (price-time priority requires processing in arrival order within a
   level). Per-order locks add overhead without enabling meaningful
   parallelism, and introduce deadlock risk during multi-fill
   scenarios.

2. **Per-level locks (one mutex per `PriceLevel`)** — allows
   concurrent access to different price levels. Rejected: a single
   aggressive order can cross multiple levels in one sweep, requiring
   locks on all touched levels simultaneously. The locking protocol
   becomes complex (ordered lock acquisition to avoid deadlock) for
   zero real-world gain on a single-symbol engine.

3. **Read-write lock on the entire book** — readers (PRINT_BOOK,
   market data snapshots) share access; writers (submit/cancel)
   acquire exclusive. Rejected: in a latency-sensitive system, even an
   uncontended `pthread_rwlock` costs ~20–40ns per acquisition. With
   one thread and no concurrent readers *inside the engine*, this is
   pure overhead. Read-only snapshots for external consumers are
   handled by the `EventSink` broadcast, not by reading the book
   directly from another thread.

4. **Multiple symbols on multiple threads, shared-nothing** — each
   symbol gets its own engine instance on its own thread. Compatible
   with this decision (it's the natural scale-out path), but not
   needed until the project handles multiple instruments. Noted as the
   future direction, not rejected — just deferred.

## Consequences

- **Positive:** Zero synchronization overhead on the matching hot path.
  Deterministic execution (same input → same output, regardless of
  timing). Trivial to reason about correctness — no data races
  possible within the engine. Latency measurements are meaningful and
  reproducible (no lock-contention variance).
- **Negative:** Burns one full CPU core per symbol even when the book
  is idle (the engine thread busy-spins). Acceptable on a dedicated
  trading server; noticeable on a developer laptop. Not parallelizable
  within a single symbol — throughput is bounded by single-core speed.
- **Enforced by:** Architecture: `engine/` and `orderbook/` contain no
  mutex, atomic, or threading headers. Phase 4's SPSC queue is the
  *only* concurrency primitive, and it lives outside the engine's
  compilation unit. Integration tests verify the engine can be driven
  from a single thread with deterministic output.
