# Phase 2 — Requirements: Benchmark Harness + Baseline Numbers

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

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
  and Trade
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

## 5. Open Questions (resolve before design.md for this phase)

1. **Workload generator distribution** — uniform random price around a
   synthetic mid-price, or something closer to a real order-flow
   distribution (e.g. geometric/log-normal price offsets, Poisson
   arrival)? A more realistic generator is more work now but gets
   reused by Phase 10's Strategy SDK for synthetic flow — worth
   deciding whether to build one shared, reusable generator now, or a
   throwaway one here and a better one later.
2. **Machine/environment control** — do you want `taskset`/CPU-pinning
   and disabling CPU frequency scaling/turbo boost for stable numbers
   in this phase, or is "numbers on my laptop, caveat noted" acceptable
   for now, with real isolation deferred to whenever you have a
   dedicated benchmarking box?
3. Should the benchmark harness be a reusable library other apps
   (Phase 10's strategy runner, later ad hoc profiling) can link
   against, or a single-purpose executable?
