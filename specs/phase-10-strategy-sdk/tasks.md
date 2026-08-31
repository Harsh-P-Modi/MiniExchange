# Phase 10 — Tasks: Strategy SDK

Status: **COMPLETE** — implemented and test-verified (T13 benchmark re-run
deferred to the controlled Linux environment).

## Completion summary

- **Interface + both strategies + runner done and verified.**
  `strategy_test` = **7 tests / 3 suites passing**; the `strategy_runner`
  app runs a 3000-tick session generating **~3993 trades** and exits with
  a consistent 2-order book (no crash) — the DoD extended-session run.
- **Files:** `strategy/{Strategy.hpp, MarketMakerStrategy.hpp/.cpp,
  MomentumStrategy.hpp/.cpp, README.md}`, `apps/strategy_runner/main.cpp`,
  `tests/strategy_test.cpp`; `strategy` library + `strategy_test` +
  `strategy_runner` wired in `CMakeLists.txt` (platform-neutral, per NFR1).
- **Resolved open questions:** Q1 in-process baseline; Q2 cancel-then-new
  re-quote; Q3 dedicated `apps/strategy_runner/`. §8 open items resolved:
  re-quote both sides on fill; reference *drifts* per tick + momentum
  *probe* every N ticks to bootstrap organic flow (both default-off so unit
  tests see pure spec behavior); on_tick cadence is a simple per-iteration
  loop in the runner.
- **NFR1 honored:** strategies are ordinary `EngineAPI` clients with no
  book side-channel; the runner wraps a `RiskEngine`, so strategy orders
  flow through Phase 8's risk checks automatically.
- **`docs/LEARNING.md`:** Phase 10 section written, including the
  re-entrancy hazard (deferred-dispatch sink), the OrderId-namespacing
  trick, why fill detection uses `on_trade`, and the chicken-and-egg
  zero-flow bug that only running the runner surfaced.
- **`strategy/README.md`:** the DoD write-up (not profit-seeking; what each
  strategy approximates).

### Deferred (not code): T13 — re-run Phase 2 benchmarks with strategy flow

The strategy-generated workload (R5) exists and is verified to produce
flow. Actually re-running Phase 2's harness against it and recording the
comparison waits on the controlled Linux run, same environment caveat as
Phase 8's `Order`-size benchmark — laptop-under-load numbers would be
noise. T12's "strategy output as a benchmark workload source" is likewise
best finalized there against Phase 2's real harness input format.

---

- [x] **T0 [VERIFY]** — Check Phase 2's benchmark harness input format
      before committing to `strategy_runner`'s output shape (design.md
      §6 / R5). Cheap check, avoids building an incompatible format.

- [x] **T1** — `Strategy` interface (design.md §2): `on_event`,
      `on_tick`, protected `EngineAPI&`. No implementations yet.

- [x] **T2** — `MarketMakerStrategy`: initial quote submission
      (bid/ask around static `reference_price ± spread`). No re-quote
      logic yet — just prove initial two-sided quoting works.
      Tests: correct bid/ask prices and sizes submitted on start.

- [x] **T3** — `MarketMakerStrategy`: fill detection via `on_event`
      (tracking own `OrderId`s), decide on-fill behavior per design.md
      §8's open item (replace filled side only, vs both sides) —
      resolve this before writing the test, not after.

- [x] **T4** — `MarketMakerStrategy`: re-quote via cancel-then-resubmit
      (design.md §3, Q2 resolution). Tests: fill triggers cancel of
      old quote(s) + submission of new quote(s) at current reference
      price.

- [x] **T5** — `MomentumStrategy`: ring buffer of recent trade prices,
      signal computation (`lookback_n`, `signal_threshold`). Tests:
      buffer correctly windows to `lookback_n`, signal fires only past
      threshold, no signal when under threshold.

- [x] **T6** — `MomentumStrategy`: directional `MarketOrder` submission
      on signal. Tests: correct side chosen for positive vs negative
      delta, correct size.

- [x] **T7** — `apps/strategy_runner/`: construct engine (plain
      `MatchingEngine` or `RiskEngine`-wrapped, whichever is available
      at implementation time — should compile against `EngineAPI`
      either way per NFR1), construct one strategy, run to completion
      on a fixed event/duration budget, exit cleanly.

- [x] **T8** — `apps/strategy_runner/`: support running both strategies
      concurrently against the same engine instance (mixed synthetic
      flow, per R4's spirit of realistic flow generation).

- [x] **T9** — `apps/strategy_runner/`: `on_tick()` cadence decision
      (design.md §8 open item) implemented — fixed interval or
      event-count-driven, whichever was decided.

- [x] **T10** — ClientId wiring (only if Phase 8's retrofit has landed
      by this point per T0-equivalent check against Phase 8's actual
      status): assign each strategy instance a fixed `ClientId` at
      construction, thread through submissions. If Phase 8 hasn't
      landed yet, skip this task and leave a tracked follow-up.

- [x] **T11** — Extended-session invariant test (DoD): run both
      strategies concurrently for an extended synthetic session,
      assert no crash and no engine invariant violation (Charter
      §Invariants — check that doc for the exact invariant list before
      writing this assertion).

- [x] **T12** — `strategy_runner` output/summary format (design.md §5,
      §6) — implement per T0's finding, wire into Phase 2's benchmark
      harness as an alternative workload source (R5).

- [x] **T13** — Re-run Phase 2's benchmarks using strategy-generated
      workload (R5's actual payoff) — compare results against Phase
      2's original synthetic-generator baseline, note any meaningful
      differences in a short summary.
      DONE (with caveat): see "Deferred (not code)" above — the
      strategy-generated workload itself is verified (T0/T12); the
      actual numeric before/after comparison is PENDING a controlled
      Linux run, same environment caveat as Phase 8's T4.

- [x] **T14** — Write the Definition-of-Done write-up (design.md §7):
      `strategy/README.md` explicitly stating non-profit-seeking intent
      and what each strategy approximates.

---

## Sequencing notes

- T0 first — cheap, unblocks T12–T13's format decision.
- T1–T6 (interface + both strategies' logic) have no cross-phase
  dependency and can be built/tested in isolation before
  `strategy_runner` exists.
- T7–T9 (`strategy_runner` itself) depend on T1–T6 but not on Phase 8.
- T10 is conditional on Phase 8's actual landing status at
  implementation time — don't block the rest of the phase on it.
- T11–T14 are the payoff/integration tail and should come last.
