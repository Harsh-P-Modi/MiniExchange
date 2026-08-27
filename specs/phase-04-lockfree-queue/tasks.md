# Phase 4 — Tasks: Lock-Free Queue

Status: **APPROVED PLAN — execute one task at a time**

**Execution rule (per `.kiro/steering/structure.md`): do exactly one
task below, then stop and wait for explicit review/approval before
starting the next one.** Update `docs/LEARNING.md` as part of each
task's own review, per `.kiro/steering/learning-doc.md`.

---

## Task 1 — `core/EngineCommand.hpp`

Create `CancelRequest` and `EngineCommand` per `design.md` §2. This is
the **canonical** `CancelRequest` for anything engine-facing — distinct
from `apps/cli/`'s separate, app-local `miniexchange::cli::CancelRequest`
(part of the CLI's own grammar type set, not touched by this task).
Nothing in `apps/cli/` changes as part of this task.

**Acceptance criteria:** header compiles; a test confirms
`std::holds_alternative` correctly distinguishes all three
alternatives (`LimitOrder`, `MarketOrder`, `CancelRequest`).

**Implements:** `design.md` §2.

---

## Task 2 — `lockfree_queue/SpscRingBuffer.hpp`, single-threaded tests first

Implement the ring buffer per `design.md` §3, tested initially with
*only one thread* (no concurrency yet — that's Task 3) to validate the
basic push/pop/full/empty logic in isolation before concurrency adds
noise to debugging.

**Acceptance criteria (GoogleTest, single-threaded):**
- `try_push` succeeds up to `Capacity` times, then returns `false`
  (R4's reject-on-full, confirmed structurally).
- `try_pop` on an empty buffer returns `false` immediately (R3).
- Push N, pop N, values come back in the same order pushed (FIFO
  behavior, single-threaded sanity check before the real concurrency
  test in Task 3).
- `static_assert` on a non-power-of-two `Capacity` fails to compile
  (verify by attempting it in a `// this shouldn't compile` comment
  block you test manually, not as part of the normal test suite).

**Implements:** `design.md` §3; `requirements.md` R1, R3, R4.

---

## Task 3 — Concurrent stress test (NFR3)

Add the two-thread producer/consumer stress test per `design.md` §5,
run under ThreadSanitizer.

**Acceptance criteria:** TSan-clean; test asserts zero lost, zero
duplicated, zero reordered items across a large (≥1,000,000 item) run.
This is the acceptance criterion for `requirements.md` R2 and R5 (SPSC
correctness, no accidental engine multi-threading implied by the
queue's existence). Also assert a coarse throughput floor (e.g. >10M
ops/sec on whatever machine this runs on, adjust if your hardware
differs) — not a real benchmark (Task 4 is), but a cheap smoke-test
catching "compiles and passes correctness but is accidentally
serializing somewhere" regressions, which pure correctness assertions
wouldn't catch.

**Implements:** `design.md` §5; `requirements.md` R2, R5, NFR3.

---

## Task 4 — Phase 2 `WorkloadEvent` alias tidy-up (moved ahead of the
benchmark task so it can reuse this)

Change `tools/workload_generator/WorkloadGenerator.hpp`'s
`WorkloadEvent` from its own `std::variant`/`CancelRequest` definition
to `#include "core/EngineCommand.hpp"` + `using WorkloadEvent =
core::EngineCommand;` — **delete** the local `struct CancelRequest`
definition entirely, don't leave it alongside the alias (per
`design.md` §2's collision note: two `CancelRequest` definitions in the
same `miniexchange` namespace won't compile).

**Acceptance criteria:** Phase 2's existing `WorkloadGenerator` tests
all still pass unchanged (this is purely a type-identity tidy-up, not a
behavior change) — same "prove the swap is invisible" acceptance
pattern used for Phase 3's `OrderPool` swap. Confirm via a full rebuild
that no other file still references the deleted local `CancelRequest`.

**Implements:** `design.md` §2 cross-reference note.

---

## Task 5 — `apps/benchmark/MutexQueue.hpp` + comparative benchmark

Implement the mutex-based baseline per `design.md` §4. The benchmark,
precisely specified (per the review that flagged the original wording
as too loose):
- **Payload:** `EngineCommand`, generated via Phase 2's
  `WorkloadGenerator` (now that Task 4's alias makes this direct — no
  separate payload-construction code needed for the benchmark).
- **Two measurement modes, not one:** (a) isolated per-operation
  latency — one thread pushes, immediately pops on the same thread,
  no real concurrency, measuring pure per-call overhead via Phase 2's
  `LatencyRecorder`; (b) real two-thread producer/consumer throughput
  — a dedicated producer thread pushes a pre-generated sequence as fast
  as `try_push` allows, a dedicated consumer thread drains via
  `try_pop`, measuring sustained ops/sec (Google Benchmark, matching
  Phase 2's R4 approach).
- **Producer behavior on full queue:** matches both structures'
  documented behavior — reject (return `false`) and move on, no retry
  loop, no spin-wait, for both `SpscRingBuffer` and `MutexQueue` (their
  `try_push`/`try_pop` share a signature specifically so this stays a
  fair comparison, per `design.md` §4).

**Acceptance criteria:** `benchmarks/results/phase-04-queue-comparison.md`
produced, with both isolated-latency and real-throughput numbers for
both structures side by side. Write-up specifically calls out *tail*
latency (P99/worst) differences, not just averages — per
`requirements.md` R6, that's where lock contention is expected to show
up worst, so an honest write-up should say whether it actually did.

**Implements:** `requirements.md` R6; `design.md` §4.

---

## Task 6 — Definition of Done audit

Confirm every item in `requirements.md` §4 is met. Confirm
`docs/LEARNING.md` explains the acquire/release memory-ordering
reasoning from `design.md` §3 at the level of detail it's written
there — this is exactly the kind of thing worth being able to explain
from scratch in an interview, not just cite as "it's correct."

**Acceptance criteria:** Phase 4's Definition of Done fully met.

---

Once Task 6 is signed off, Phase 5 (TCP gateway) can begin — it will
consume this phase's `SpscRingBuffer<EngineCommand>` directly as its
producer-to-engine channel.
