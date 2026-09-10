# Phase 8 — `Order` size change: matching-path benchmark

**64-byte baseline:** controlled Linux run — Ubuntu 24.04.5, Intel Core
i5-13500H, kernel 7.0.0-31-generic, RelWithDebInfo, `taskset -c 2,3`,
governor=performance, turbo=off. Commit `49d4a15` (the last commit before
the `owner` field was added), with the benchmark's per-iteration pool
capacity patched from 1,000,000 to 4,096 — see "Status" for why.

**72-byte side:** controlled run still pending — see "Status".

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

## Status

### 64-byte `Order` — measured (controlled Linux, 49d4a15)

| Operation | Avg (ns) | Median (ns) | P99 (ns) | Max (ns) |
|---|---|---|---|---|
| ADD (no match) | 244.2 | 242.0 | 301.0 | 6,295 |
| ADD (1 fill) | 110.0 | 104.0 | 174.0 | 2,312 |
| ADD (10 fills) | 798.3 | 792.0 | 886.0 | 6,219 |
| ADD (100 fills) | 8,493.2 | 8,440.0 | 10,133.0 | 149,549 |
| CANCEL (front) | 71.8 | 69.0 | 102.0 | 608 |
| CANCEL (back) | 70.8 | 68.0 | 100.0 | 1,734 |

Mixed workload (60% limit / 10% market / 30% cancel): **9.57M orders/sec**
(best of 10 reps).

This is the first *controlled* capture of these numbers — every earlier
table (`phase-02-baseline.md`, `phase-03-pooled.md`) was an uncontrolled
Windows laptop and self-describes as "dominated by system noise". Treat
this as the 64-byte reference, not the Phase 2 medians: e.g. ADD
(100 fills) is 8,440 ns here vs the Windows table's 19,500 ns — the
Windows figure was ~2.3× inflated by scheduler/turbo noise, nothing
structural.

### 72-byte `Order` — controlled run still required

Not yet captured under matched conditions. The one partial run that
exists (commit 164e03b, current tree) is **not usable** for the delta:

- Its `latency_bench.cpp` still constructs each per-iteration engine with
  the **default 1,000,000-slot pool**. Construction is untimed, but
  `new Order[1'000'000]` + free-list init strides ~72 MB and evicts every
  cache level and the TLB, so the *timed* op then runs cold. The 64-byte
  run above used a 4,096-slot pool (~256 KB, stays in L2) — hence the
  pool-cap patch noted at the top.
- That run reported ADD (no match) median **1,442 ns** and ADD (1 fill)
  **710 ns** — 6× and 7× the 64-byte numbers. That gap is the cold-cache
  artifact of the 72 MB stride, not 8 bytes of struct growth, and cannot
  be subtracted out from two data points. The run also stalled after row
  2 (1 M-slot alloc × 10,000 iterations is minutes of page-fault churn),
  so **ADD (10 fills)** and **ADD (100 fills)** — the only rows that carry
  a cache-line-straddle signal — were never recorded.

### To close this

Run the 72-byte harness from a commit whose `latency_bench.cpp` caps the
bench pool at 4,096 (that landed in `5168fb0`), on the same box:

```
git worktree add /tmp/mx-p8-after 5168fb0
cmake -S /tmp/mx-p8-after -B /tmp/mx-p8-after/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/mx-p8-after/build --target benchmark_harness
( cd /tmp/mx-p8-after && stdbuf -oL -eL taskset -c 2,3 ./build/benchmark_harness )
git worktree remove /tmp/mx-p8-after --force
```

(`bench_finish.sh` at the repo root does exactly this, plus the R9 trace.)
Then compare the `ADD (10 fills)` / `ADD (100 fills)` medians against the
64-byte table above. ADD (no match) and CANCEL should be flat (they touch
≤ 2 orders); any real cost of the 64→72 change shows on the deep-sweep
rows or nowhere.

## Expectation (hypothesis to confirm, not a measured result)

The change adds no operations and no allocations — it only widens a
struct. The expected effect is therefore *at most* a small, possibly
unmeasurable, increase on the multi-fill ADD rows from the extra
cache-line touches, and no change to CANCEL or single-order ADD. If a
future controlled run shows a material regression on the deep-sweep
rows, the mitigation is already identified (32-bit index pool links,
Phase 3-scoped) and deliberately out of scope for Phase 8.
