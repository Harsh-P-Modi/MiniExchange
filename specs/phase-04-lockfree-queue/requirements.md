# Phase 4 — Requirements: Lock-Free Queue

Status: **APPROVED** — Open Questions resolved below; `design.md` and
`tasks.md` are built on this version.

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

## 5. Open Questions — Resolved

1. **SPSC or MPSC — RESOLVED: SPSC now.** Phase 5 introduces exactly
   one TCP gateway I/O thread, so SPSC is sufficient through Phase 8.
   Documented explicitly as planned future rework: Phase 9 (FIX)
   introduces a second concurrent adapter thread, at which point this
   queue genuinely needs revisiting (either a second SPSC queue per
   producer thread with the engine polling both, or an actual MPSC
   structure) — flagging now so Phase 9 doesn't rediscover this from
   scratch.
2. **Back-pressure policy — RESOLVED: reject, don't block.** The
   producer's `try_push` returns `false` on a full queue; the caller
   (whichever adapter) is responsible for translating that into
   whatever rejection its protocol supports. Blocking would defeat part
   of the point of using a lock-free queue in the first place, and a
   bounded spin-then-reject is just a slower way of arriving at the
   same "reject" outcome — reject immediately.
3. **Ring buffer capacity — RESOLVED: 4096, and it's a compile-time
   template parameter, not a runtime value** (see `design.md` §6 item 1
   for why). Different call sites can use different capacities by
   instantiating the template differently; there's no single global
   value being fixed.
