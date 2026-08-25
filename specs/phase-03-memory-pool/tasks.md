# Phase 3 — Tasks: Memory Pool

Status: **APPROVED PLAN — execute one task at a time**

**Execution rule (per `.kiro/steering/structure.md`): do exactly one
task below, then stop and wait for explicit review/approval before
starting the next one.** Update `docs/LEARNING.md` as part of each
task's own review, per `.kiro/steering/learning-doc.md`.

---

## Task 1 — `orderbook/OrderPool.hpp` / `.cpp`, standalone

Implement `OrderPool` exactly per `design.md` §2, tested entirely on
its own (no `OrderBook`/`MatchingEngine` involvement yet).

**Acceptance criteria (GoogleTest):**
- `acquire()` on a fresh pool returns a valid, distinct pointer each
  call, up to `capacity()` times.
- `acquire()` past capacity returns `nullptr` (R4's structural
  precondition — the `PoolExhausted` translation happens in Task 3, not
  here).
- `release()` then `acquire()` returns the *same* address (proves the
  free list actually reuses slots rather than silently leaking them).
- `available()` correctly reflects capacity minus currently-acquired
  count after a mix of acquires/releases.
- Debug-build assertion fires on an out-of-range free-list index (a
  deliberately corrupted test scenario, per `design.md` §6 item 1).

**Implements:** `design.md` §2.

---

## Task 2 — `OrderBook` ownership swap

Change `OrderBook::orders_` from owning `unique_ptr<Order>` to a
non-owning `Order*` index, backed by a new `OrderPool pool_` member,
per `design.md` §3. `insert()` and `remove()` updated accordingly.

**Acceptance criteria:** **every existing Phase 1 `OrderBook` test
passes unchanged** — this is the acceptance criterion, not a new test
suite. If any Phase 1 test needs modification to pass, that's a signal
this swap wasn't actually invisible as designed, and worth stopping to
discuss before continuing.

**Implements:** `design.md` §3; `requirements.md` R2, R3, R5.

---

## Task 3 — `PoolExhausted` wiring

Add `EngineResult::PoolExhausted` (Task in `core/Events.hpp`), wire
`MatchingEngine::submit` to check `OrderBook::insert`'s `nullptr`
return and translate it to `EngineResponse{PoolExhausted, {},
requested_qty}`, per `design.md` §4.

**Acceptance criteria (GoogleTest):**
- Construct a small-capacity `MatchingEngine` (e.g. capacity 2), fill
  it, confirm the 3rd `ADD` returns `PoolExhausted`.
- Confirm no side effects from the rejected order: book state
  unchanged, `ever_seen_ids_` (Phase 1 §2.1) does **not** gain the
  rejected order's ID — same "rejections have zero side effects"
  discipline as every other Phase 1 rejection path.
- Confirm a subsequent `CANCEL` of an existing order frees a slot, and
  the *next* `ADD` then succeeds (proves the pool actually recycles
  under real engine use, not just in Task 1's isolated test).

**Implements:** `design.md` §4; `requirements.md` R4.

---

## Task 4 — Full Phase 1 regression pass

Run the entire Phase 1 test suite against the now-pooled engine.

**Acceptance criteria:** 100% pass, zero modifications needed to any
Phase 1 test file. Record this explicitly in `docs/LEARNING.md` as the
concrete proof that the ownership-boundary design decision from Phase 1
`design.md` §8 paid off as predicted — this is a good, quotable data
point for interviews ("I designed the ownership boundary in Phase 1
specifically so Phase 3's pool swap would be invisible, and it was —
zero test changes needed").

**Implements:** `requirements.md` §4 Definition of Done, item 1.

---

## Task 5 — Benchmark comparison vs. Phase 2 baseline

Re-run Phase 2's `apps/benchmark` suite against the pooled engine,
producing `benchmarks/results/phase-03-pooled.md` in the same format as
Phase 2's results file, with an explicit side-by-side delta table
(Phase 2 baseline vs. Phase 3 pooled, per operation).

**Acceptance criteria:** results file exists with a clear before/after
comparison; the write-up interprets the numbers honestly per the
Charter's benchmark philosophy — if allocation-heavy operations (ADD)
improved substantially and non-allocation-heavy ones (CANCEL, which was
already O(1) via the intrusive list, not via allocation) didn't move
much, say so explicitly rather than implying uniform improvement.

**Implements:** `requirements.md` R6.

---

## Task 6 — Definition of Done audit

Confirm every item in `requirements.md` §4 is met. Confirm
`docs/LEARNING.md` covers `OrderPool` per the full ten-item checklist
in `.kiro/steering/learning-doc.md` — this module is a good candidate
for genuinely using all ten, since it's real, non-trivial systems work
(fixed allocator design, intrusive free list) with real alternatives
that were actually rejected (§6 above).

**Acceptance criteria:** Phase 3's Definition of Done fully met.

---

Once Task 6 is signed off, Phase 4 (lock-free queue) can begin.
