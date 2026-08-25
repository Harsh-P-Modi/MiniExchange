# Phase 2 — Tasks: Benchmark Harness + Baseline Numbers

Status: **APPROVED PLAN — execute one task at a time**

**Execution rule (per `.kiro/steering/structure.md`): do exactly one
task below, then stop and wait for explicit review/approval before
starting the next one.** Update `docs/LEARNING.md` as part of each
task's own review, per `.kiro/steering/learning-doc.md` — not a
separate checkpoint.

---

## Task 1 — `tools/workload_generator/`

Create `WorkloadConfig`, `CancelRequest`, `WorkloadEvent`, and
`WorkloadGenerator` exactly per `design.md` §2. Seed the RNG from
`config.seed` (R5) — no other source of randomness anywhere in this
class.

**Acceptance criteria (GoogleTest):**
- Same `seed` → identical generated sequence across two separate
  `WorkloadGenerator` instances (confirms R5's reproducibility).
- Different `seed` → different sequence.
- Generated `CANCEL` events only ever reference `OrderId`s the
  generator itself previously produced as `LimitOrder`s (never a
  `MarketOrder`'s id, since market orders never rest — matches Phase
  1's R10, and the generator should respect that even though it's not
  talking to a real engine).
- Mix ratio roughly matches `add_limit_ratio`/`add_market_ratio`/
  `cancel_ratio` over a large generated sample (statistical check, not
  exact).

**Implements:** `design.md` §2; `requirements.md` §5 item 1.

---

## Task 2 — `apps/benchmark/` build skeleton

Add the CMake target: links `engine`, `orderbook`, `core`,
`tools/workload_generator`, and Google Benchmark (already available via
Phase 1's `FetchContent` setup). No benchmark code yet — just confirm
it builds and an empty `main` links correctly against everything it'll
need.

**Acceptance criteria:** `cmake --build build --target benchmark`
succeeds with an empty/trivial `main.cpp`.

---

## Task 3 — `apps/benchmark/LatencyRecorder.hpp`

Implement per `design.md` §3: `record`, `avg_ns`, `median_ns`,
`p99_ns`, `max_ns`.

**Acceptance criteria (GoogleTest):** feed a known set of synthetic
durations (e.g. 1..100 ns), confirm `avg`/`median`/`p99`/`max` match
hand-computed expected values exactly — this is pure arithmetic, so
the test should be exact, not approximate.

**Implements:** `design.md` §3.

---

## Task 4 — `bench_add_no_match` (R1)

Implement per `design.md` §4: fresh `MatchingEngine` per iteration
(untimed construction), measure only the single non-crossing `ADD`
call, record via `LatencyRecorder`.

**Acceptance criteria:** runs and produces avg/median/P99/max numbers
that are at least internally sane (median ≤ P99 ≤ max, avg roughly in
range) — this task doesn't need to hit any target number, just produce
trustworthy measurement code.

**Implements:** `requirements.md` R1; `design.md` §4, §6.

---

## Task 5 — `bench_add_with_match` (R2)

Implement per `design.md` §4, parameterized by `fill_count ∈ {1, 10,
100}`: untimed setup inserts `fill_count` crossing resting orders, time
only the one incoming order that consumes them all.

**Acceptance criteria:** three sets of numbers (one per `fill_count`),
each internally sane; confirm the *shape* of scaling is reported (even
if you don't yet know whether it should be linear — that's an
observation for the results write-up, not a pass/fail test).

**Implements:** `requirements.md` R2; `design.md` §4.

---

## Task 6 — `bench_cancel` (R3)

Implement per `design.md` §4: front-of-queue and back-of-queue cancel,
each measured separately.

**Acceptance criteria:** both variants produce numbers; explicitly
compare them in the results write-up (this is the benchmark whose
*point* is confirming they're statistically indistinguishable — say so
explicitly if they are, and flag it clearly if they're not, since that
would indicate the intrusive-list design isn't delivering what Phase 1
claimed).

**Implements:** `requirements.md` R3; `design.md` §4.

---

## Task 7 — `BM_SustainedThroughput` (R4)

Implement per `design.md` §5: pre-generate 100k events via
`WorkloadGenerator` (untimed), run through a fresh engine per
repetition, report via Google Benchmark's `SetItemsProcessed`.

**Acceptance criteria:** produces an orders/sec number via standard
Google Benchmark output; confirm event pre-generation genuinely
happens outside the timed region (code review check, per NFR2).

**Implements:** `requirements.md` R4, R7 (Google Benchmark's own
warm-up/repetition handling satisfies R7 for this case); `design.md` §5.

---

## Task 8 — `ResultsWriter` + `benchmarks/results/phase-02-baseline.md`

Implement a small formatter that takes the `LatencyRecorder` results
from Tasks 4–6 and the throughput result from Task 7 and writes them
into the markdown table format specified in `design.md` §7. Wire it
into `apps/benchmark/main.cpp` so running the benchmark executable
produces/updates this file directly (not a manual copy-paste step).

**Acceptance criteria:** running the benchmark executable produces
`benchmarks/results/phase-02-baseline.md` with all tables populated,
matching `design.md` §7's format; the environment line is filled in
honestly (not left as a placeholder).

**Implements:** `requirements.md` R6; `design.md` §7.

---

## Task 9 — `scripts/run_benchmarks.sh`

A wrapper script that runs the benchmark executable under `taskset`
(pinned core, configurable) with a comment explaining how to disable
turbo boost/frequency scaling manually (this varies by machine/BIOS, so
document rather than automate). Running the benchmark *without* this
script remains fully supported — it's a recommendation, not a
requirement (per `requirements.md` §5 item 2's resolution).

**Acceptance criteria:** script runs successfully and produces the same
results file as running the executable directly, just with CPU pinning
applied; the results file's environment line reflects whether the
script was used for that run.

**Implements:** `requirements.md` §5 item 2 (resolved).

---

## Task 10 — Definition of Done audit

Confirm `benchmarks/results/phase-02-baseline.md` exists with numbers
for R1–R4 (Phase 1's baseline may not hit the Charter's ~100k
orders/sec target yet — that's fine and expected, per
`requirements.md` §4). Confirm `docs/LEARNING.md` has entries for
`WorkloadGenerator`, `LatencyRecorder`, and the R1–R3-vs-R4 measurement-
approach decision (§6 of `design.md` is exactly the kind of thing
`LEARNING.md` should capture — it's a real, non-obvious design choice).
Confirm the results file is referenced from `docs/LEARNING.md`, per
`requirements.md` R6 and the steering policy.

**Acceptance criteria:** Phase 2's Definition of Done
(`requirements.md` §4) is fully met.

---

Once Task 10 is signed off, Phase 3 (memory pool) can begin — same
process: resolve its Open Questions, then `design.md`, then `tasks.md`.
