# Phase 8 — `Order` size change: matching-path benchmark

Environment (intended): same uncontrolled Windows laptop as the Phase 2
baseline and Phase 3 results — no CPU pinning, no turbo-boost control.
Build: RelWithDebInfo (`build_check`, msys2 ucrt64 GCC + Ninja).

## What changed and why we measure it

Phase 8 / T2 added a `ClientId owner` field to the resting `Order`
struct so the engine can attribute each resting order to a client (for
self-trade prevention, R5). This grew the struct:

| | `sizeof(Order)` | Cache lines (64B) |
|---|---|---|
| Before (Phases 1–7) | 64 bytes | 1 |
| After (Phase 8 / T2) | 72 bytes | 2 |

This is a **verified structural fact**, not an estimate: the change is
pinned by `static_assert(sizeof(Order) == 72)` in `core/Order.hpp`,
which the whole codebase compiles against. If the size were anything
other than 72, the build would fail. (Before the field was added the
struct was exactly 64; see `docs/LEARNING.md` Phase 8 / T2 for the
field-by-field layout and why it cannot be packed back to 64 without
Phase 3-style 32-bit index pool links.)

The hot path this could affect: `MatchingEngine::match_against_book`
walks a chain of resting `Order`s via `Order::next`, reading each one's
`price`/`quantity`. A resting order now straddles two cache lines, so in
the worst case each examined order costs an extra line fetch.

## Measurement status: PENDING a controlled run

The numeric latency/throughput comparison for this change was **not
captured in this working session** — the local shell used to drive the
benchmark harness was unreliable (commands intermittently failed to
produce output), so no trustworthy numbers could be recorded. Rather
than paste a half-captured or noisy figure, the measurement is left
explicitly pending.

This is consistent with the honesty the existing baseline docs already
apply to themselves: both `phase-02-baseline.md` and `phase-03-pooled.md`
conclude that numbers gathered on this uncontrolled Windows laptop are
"dominated by system noise" and recommend re-running on Linux with
`taskset 1`, `performance` governor, and turbo-boost disabled for any
production-grade comparison. A cache-line-straddle effect of a few
nanoseconds per examined order is well below that noise floor on this
box and would not be distinguishable here anyway.

### How to produce the numbers (reproduction steps)

The harness is already wired and builds clean with the 72-byte `Order`:

```
cmake --build build_check --target benchmark_harness
./build_check/benchmark_harness.exe --benchmark_min_time=0.1s
```

It prints the six single-operation latency rows (ADD no-match / 1 / 10 /
100 fills, CANCEL front / back) and the mixed-workload throughput, and
writes a comparison table. To attribute this change specifically:

1. Record numbers on the current tree (72-byte `Order`).
2. Compare against the Phase 2 baseline table below. The most
   size-sensitive rows are **ADD (10 fills)** and **ADD (100 fills)**,
   since those walk the longest resting-order chains and thus touch the
   most `Order` cache lines. ADD (no match) and CANCEL should be
   essentially unaffected (they touch at most one or two orders).
3. Run on the Linux/CI environment for the authoritative figure, since
   the delta is expected to be at or below this laptop's noise floor.

### Phase 2 baseline (for the eventual comparison)

| Operation | Median (ns) |
|---|---|
| ADD (no match) | 900 |
| ADD (1 fill) | 600 |
| ADD (10 fills) | 2300 |
| ADD (100 fills) | 19500 |
| CANCEL (front) | 300 |
| CANCEL (back) | 200 |
| Mixed throughput | 2.92M orders/sec |

## Expectation (hypothesis to confirm, not a measured result)

The change adds no operations and no allocations — it only widens a
struct. The expected effect is therefore *at most* a small, possibly
unmeasurable, increase on the multi-fill ADD rows from the extra
cache-line touches, and no change to CANCEL or single-order ADD. If a
future controlled run shows a material regression on the deep-sweep
rows, the mitigation is already identified (32-bit index pool links,
Phase 3-scoped) and deliberately out of scope for Phase 8.
