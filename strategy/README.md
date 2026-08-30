# Strategy SDK (Phase 10)

A minimal `Strategy` interface plus two illustrative implementations, whose
**sole purpose is to generate realistic synthetic order flow** to exercise
and demonstrate the rest of the exchange.

## These are NOT profit-seeking

To be unambiguous (per the Charter's explicit non-goal and this phase's
Definition of Done): these strategies are **not** evaluated, tuned, or
intended to make money. A strategy that "loses money" while producing
interesting, realistic order flow is a success for this phase's actual
goal. There is no P&L tracking, no fair-value model, no risk budgeting —
that would be a different project.

## What each strategy approximates

- **`MarketMakerStrategy`** approximates a *passive liquidity provider*:
  it continuously posts a two-sided quote (a bid below and an ask above a
  reference price) and re-quotes when hit, generating steady two-sided
  resting-order traffic. This is the kind of participant that populates
  the book with depth.

- **`MomentumStrategy`** approximates a *reactive directional
  participant*: it watches the trade tape, and when price has moved
  enough over a short window, fires a market order in that direction —
  trend-following bursts. This is the kind of participant that consumes
  liquidity and moves price.

Run together, they give the engine a **mixed flow** — passive quoting plus
reactive aggression — that is far more realistic than a single
uniform-random order generator. That mixed flow is what makes the
re-run of Phase 2's benchmarks (R5) meaningful: it stresses the matching
path with fills, cancels, and re-quotes in realistic proportions rather
than independent random events.

## Design notes (resolved open questions)

- **In-process (design.md Q1):** the runner drives strategies directly
  against `EngineAPI` in one process. A TCP-client variant (real network
  latency in the loop) is a possible stretch, not required here.
- **Cancel-then-new re-quote (Q2):** the market maker re-quotes with two
  `EngineAPI` calls (cancel old, submit new). No Cancel-Replace order type
  was introduced — that stayed out of scope in Phase 1.
- **Bootstrapping the tape (design.md §8):** a purely trade-reactive
  strategy on an empty book would never act (no trade → no signal → no
  order → no trade). Two small, *documented and configurable* mechanisms
  break that chicken-and-egg so the runner actually produces flow:
  - the market maker's reference price *drifts* each tick
    (`drift_per_tick`), re-quoting at new levels;
  - the momentum strategy fires a periodic *probe* order
    (`probe_every_ticks`) that crosses a resting quote to seed the tape.
  Both default to *off* (0), so the strategies are purely
  spec-behavioral in unit tests; the runner turns them on to generate a
  live session.

## Running

```bash
./build/strategy_runner                 # both strategies, 5000 ticks
./build/strategy_runner --ticks=3000    # custom session length
./build/strategy_runner --mm-only       # market maker alone
./build/strategy_runner --momentum-only # momentum alone
```

It prints a one-line summary (ticks, total trades, final book size) on
exit — a quick sanity signal that the session generated flow and ended
with a consistent book.

## NFR1 — strategies are ordinary `EngineAPI` clients

Strategies never bypass `EngineAPI` and never read book state through a
side channel. They submit through the same input port every adapter uses,
and learn about outcomes only through notifications (`on_response`,
`on_trade`). Because the runner builds them against `EngineAPI`, they run
through Phase 8's `RiskEngine` decorator exactly as the TCP and FIX
gateways do — proving the port abstraction generalizes to an algorithmic
client with no special-casing.
