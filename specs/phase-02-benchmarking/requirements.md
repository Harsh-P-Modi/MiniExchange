# Phase 2 — Requirements: Benchmark Harness + Baseline Numbers

Status: **APPROVED** — Open Questions resolved below; `design.md` and
`tasks.md` are built on this version.

## 1. Scope

`apps/benchmark/` — a Google Benchmark harness measuring Phase 1's
`MatchingEngine` as-built (the `std::map` + intrusive-list baseline).
Produces the first real numbers against the Charter's performance
targets. No optimization happens in this phase — that's Phase 3+;
this phase only measures and records.

Out of scope: any code change to `engine/`/`orderbook/`/`core/` beyond
what's needed to make them benchmarkable (e.g. exposing a way to reset
engine state between benchmark iterations).

## 2. Functional Requirements (EARS)

- R1: THE HARNESS SHALL measure single-`ADD` latency (Limit order, no
  crossing liquidity — pure insert path).
- R2: THE HARNESS SHALL measure `ADD`-causing-a-match latency,
  parameterized by number of resting orders consumed (1, 10, 100) to
  show how latency scales with fill count.
- R3: THE HARNESS SHALL measure `CANCEL` latency, both for orders
  resting at the front and at the back of a price level's queue
  (should be identical — O(1) either way — this is itself a useful
  benchmark that proves the intrusive-list design works as claimed).
- R4: THE HARNESS SHALL measure sustained throughput (orders/sec) under
  a randomized mixed workload (`ADD`/`CANCEL`/`MARKET` in some ratio).
- R5: THE HARNESS SHALL use a fixed random seed for the workload
  generator, so runs are reproducible across machines/sessions.
- R6: Results SHALL be recorded in `benchmarks/results/phase-02-baseline.md`
  in the same latency-breakdown format the Charter's performance
  targets table uses (avg/median/P99/worst), not just throughput.
- R7: THE HARNESS SHALL warm up (discard early iterations) before
  recording measured iterations, per Google Benchmark best practice —
  cold-cache/cold-branch-predictor numbers aren't representative.

## 3. Non-Functional Requirements

- NFR1: Build in `RelWithDebInfo` (optimizations on, symbols kept) —
  `Debug` numbers are meaningless for latency claims.
- NFR2: No heap allocation inside the *measured* region of any
  benchmark that isn't itself measuring allocation — e.g. pre-generate
  the random workload before starting the timer.

## 4. Definition of Done

- Numbers recorded for R1–R4, matching the Charter's target table
  format (§ Performance Targets), even though Phase 1's `std::map`
  baseline may not yet hit the ~100k orders/sec Phase-2 target — the
  point of this phase is establishing *what the baseline actually is*,
  not necessarily hitting the target on the first measurement.
- `benchmarks/results/phase-02-baseline.md` exists and is referenced
  from `docs/LEARNING.md` per the steering policy.

## 5. Open Questions — Resolved

1. **Workload generator distribution — RESOLVED: build the shared,
   reasonably realistic generator now, not a throwaway.** Phase 10
   already commits to needing the same kind of generator, so this
   isn't speculative reuse (which would normally argue for waiting) —
   it's already on the roadmap. Distribution: log-normal price offsets
   around a configurable mid-price (mimics real order clustering near
   the touch — cheap to do better than uniform), configurable quantity
   distribution, configurable ADD/CANCEL/MARKET mix ratio, fixed seed
   (R5). Lives in a new top-level `tools/workload_generator/` — not
   `apps/` (it's not an executable) and not `adapters/` (it doesn't
   translate an external protocol into `EngineAPI` calls; it generates
   synthetic ones directly).
2. **Machine/environment control — RESOLVED: document, don't enforce.**
   A `scripts/run_benchmarks.sh` wrapper using `taskset` is written and
   recommended, but the harness itself doesn't require it. Every
   results file explicitly states whether isolation was used for that
   run — "numbers on a laptop, caveat noted" is acceptable, dishonest
   silence about environment is not.
3. **Reusable library vs. single-purpose executable — RESOLVED: split
   answer.** The *workload generator* (§ above) is the shared library,
   since Phase 10 genuinely reuses it. The *benchmark harness itself*
   (the `BENCHMARK()` registrations and measurement code) stays
   `apps/benchmark`-only — Phase 10 runs strategies, not Google
   Benchmark, so there's nothing there for it to share.

## 6. Measurement Approach — R1–R3 vs. R4 (new, settled during design)

Google Benchmark is well-suited to **throughput** measurement (R4:
ops/sec under sustained load) but not natively to **percentile
latency** reporting (R1–R3 need avg/median/P99/worst per R6, which
Google Benchmark's standard repetition statistics don't directly give
you). Rather than force-fit percentile reporting out of a tool that
isn't built for it:
- R1–R3 (single-operation latency: ADD, ADD-with-match, CANCEL) use a
  small custom `LatencyRecorder` that captures raw per-operation
  `std::chrono` durations into a vector and computes avg/median/P99/max
  directly — simple, honest, and exactly matches what R6 asks for.
- R4 (sustained throughput) uses standard Google Benchmark
  `BENCHMARK()`/`BENCHMARK_MAIN()` macros, which are the right tool for
  that specific measurement.
- Both write into the same `benchmarks/results/phase-02-baseline.md`
  (R6), just via different collection mechanisms internally.
