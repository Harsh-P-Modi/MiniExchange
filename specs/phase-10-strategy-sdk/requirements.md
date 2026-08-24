# Phase 10 — Requirements: Strategy SDK

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

## 1. Scope

A minimal `Strategy` interface plus two illustrative implementations
(market maker, momentum) whose sole purpose — per the Charter's
explicit non-goal — is generating realistic synthetic order flow to
exercise and demonstrate the rest of the exchange. These are **not**
evaluated or tuned for profitability; a strategy that "loses money" in
a way that still generates interesting, realistic order flow is a
success for this phase's actual goal.

## 2. Functional Requirements (EARS)

- R1: THE SDK SHALL define a `Strategy` interface with (at minimum) a
  callback for market data/trade events (reusing `EventSink`'s shape
  from Phase 1/6 rather than inventing a parallel notification
  mechanism) and a method to submit orders through an injected
  `EngineAPI`-compatible handle.
- R2: `MarketMakerStrategy` SHALL quote a symmetric bid/ask around a
  configurable reference price with configurable spread and size, and
  SHALL re-quote after being filled (cancel-and-replace or
  cancel-then-new, per Open Questions).
- R3: `MomentumStrategy` SHALL derive a simple directional signal from
  recent trade prices (e.g. last-N-trade price delta) and submit
  orders in that direction — the signal can be intentionally naive;
  sophistication is explicitly not the goal here.
- R4: Both strategies SHALL be runnable against the live engine
  in-process (simplest path) via a new `apps/strategy_runner/` (or
  reusing `apps/replay/`, per Open Questions).
- R5: Strategy activity SHALL be usable as the workload generator for
  Phase 2-style benchmarking under more realistic order-flow shape than
  Phase 2's original synthetic generator — this phase's output feeds
  back into re-running Phase 2's benchmarks with a better workload, not
  just existing standalone.

## 3. Non-Functional Requirements

- NFR1: Strategies SHALL NOT bypass `EngineAPI` — they are ordinary
  callers of the same port every adapter uses, proving the port
  abstraction generalizes to "algorithmic client" as well as
  "human/network client."

## 4. Definition of Done

- Both strategies run for an extended synthetic session without
  crashing or violating any engine invariant (Charter §Invariants).
- A short write-up explicitly states these are not profit-seeking and
  explains what "realistic order flow" they're meant to approximate.

## 5. Open Questions (resolve before design.md for this phase)

1. **In-process vs. over TCP** — running strategies in-process against
   `MatchingEngine` directly is simplest and sufficient to generate
   flow; running them as TCP clients (Phase 5) is more realistic
   (includes real network latency in the loop) but more work. Which do
   you want as the Phase 10 baseline, with the other as an optional
   stretch?
2. **Market maker re-quote mechanics** — cancel-then-resubmit (two
   engine calls) vs. a hypothetical cancel-replace order type (not
   currently in scope per Phase 1's explicit deferral of
   Cancel-Replace) — confirms this phase stays within Phase 1's
   existing order-type scope rather than quietly reopening it.
3. Does this phase warrant its own `apps/strategy_runner/`, or does it
   make more sense folded into `apps/replay/` since both are "drive the
   engine with synthetic/recorded flow, not a human or network client"?
