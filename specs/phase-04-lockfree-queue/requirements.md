# Phase 4 — Requirements: Lock-Free Queue

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

## 1. Scope

A ring-buffer-based queue connecting a future producer thread (Phase 5's
TCP gateway, eventually others) to the single matching-engine thread,
benchmarked against a `std::mutex`-guarded queue baseline. This is the
mechanism that lets `engine/` stay single-threaded (Charter principle)
while still being fed by a multi-threaded adapter layer.

Out of scope: any actual network code (that's Phase 5) — this phase
builds and benchmarks the queue in isolation, with a synthetic
producer/consumer test harness standing in for the real TCP thread.

## 2. Functional Requirements (EARS)

- R1: THE QUEUE SHALL be a fixed-capacity ring buffer using atomic
  head/tail indices — no dynamic resizing at runtime.
- R2: THE QUEUE SHALL support one producer thread and one consumer
  thread without locks (SPSC) as the Phase 4 baseline — see Open
  Questions for whether MPSC is needed sooner than Phase 9.
- R3: WHEN the consumer (matching thread) polls an empty queue, THE
  QUEUE SHALL return immediately (non-blocking poll) rather than
  blocking — the matching thread's loop structure (poll, process if
  available, repeat) is its own concern, not the queue's.
- R4: WHEN the producer attempts to enqueue onto a full queue, THE
  QUEUE SHALL apply a defined back-pressure policy — see Open
  Questions, this needs an explicit decision, not a default.
- R5: THE ENGINE SHALL remain single-threaded internally — this queue
  is strictly the boundary between adapter thread(s) and the engine
  thread; it must not be used to justify or enable concurrent access
  to `OrderBook`/`MatchingEngine` from multiple threads.
- R6: Benchmark comparison: mutex+`std::queue` baseline vs. this
  lock-free ring buffer, under a synthetic producer/consumer workload,
  measuring both throughput and enqueue/dequeue latency (especially
  tail latency — this is precisely where lock contention is expected
  to show up worst).

## 3. Non-Functional Requirements

- NFR1: Correct memory ordering (acquire/release, not
  `memory_order_seq_cst` used as a crutch — the benchmark and the
  design write-up should demonstrate you understand *why* each
  ordering choice is sufficient, per the Charter's documentation
  discipline).
- NFR2: Head and tail indices padded/aligned to avoid false sharing
  between producer and consumer cache lines.
- NFR3: Correctness verified beyond the benchmark itself — e.g. a
  stress test with ThreadSanitizer that produces/consumes a large,
  known sequence and checks nothing is lost, duplicated, or reordered.

## 4. Definition of Done

- Ring buffer implemented and passing a TSan-clean stress test.
- Benchmark numbers recorded: mutex-based vs. lock-free, latency
  distribution (not just throughput) for both.
- Write-up explains the memory-ordering choices, not just "it's atomic."

## 5. Open Questions (resolve before design.md for this phase)

1. **SPSC or MPSC?** Phase 5 introduces one TCP gateway thread (single
   producer, fine for SPSC). But Phase 9 (FIX) and any future adapter
   running concurrently would mean multiple producer threads feeding
   one engine — should this queue be MPSC from the start (more complex
   now, no rework later), or SPSC now with an explicit note that Phase
   9 may require revisiting this design?
2. **Back-pressure policy when full** — block the producer thread
   (defeats some of the purpose of a lock-free queue if it can still
   block), drop the incoming order and return a rejection upstream
   (requires a response path back to the producer, which doesn't fully
   exist until Phase 5), or spin-wait with a bound and then reject?
3. **Ring buffer capacity** — what's a reasonable default, and should
   it be configurable per adapter, or fixed globally?
