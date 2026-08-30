# Phase 10 — Design: Strategy SDK

Status: **DRAFT — pending your approval before tasks.md is executed**

Resolves the three open questions from requirements.md as follows
(flagging as proposed, not yet confirmed — override before treating
this as final):

- **Q1 (in-process vs TCP):** in-process against `EngineAPI` is the
  Phase 10 baseline. Running a strategy as a TCP client (using Phase
  9's... no — using Phase 5's TCP gateway, reusing the FIX or raw
  wire protocol) is a valid stretch goal, not required for this
  phase's Definition of Done.
- **Q2 (re-quote mechanics):** cancel-then-resubmit (two `EngineAPI`
  calls: `cancel()` old quote, `submit()` new quote). Deliberately
  stays within Phase 1's existing order-type scope rather than
  introducing Cancel-Replace.
- **Q3 (strategy_runner vs replay):** new `apps/strategy_runner/`.
  `apps/replay/` plays back deterministic recorded flow with no
  feedback loop; strategies are stateful and react to live
  `EngineResponse`/`Trade` events (a fill triggers a re-quote, a trade
  price updates the momentum signal). Folding a live feedback loop
  into a playback tool would give `replay` two different control-flow
  shapes depending on mode. Flagging this as the one genuine judgment
  call here — reasonable to fold them if you'd rather have one
  synthetic-flow entry point.

---

## 1. Module layout

```
strategy/
  Strategy.hpp           — interface (R1)
  MarketMakerStrategy.hpp/.cpp
  MomentumStrategy.hpp/.cpp
apps/strategy_runner/
  main.cpp                — wires a Strategy to a live EngineAPI handle
```

## 2. `Strategy` interface (R1)

```cpp
class Strategy {
public:
    virtual ~Strategy() = default;

    // Reuses EventSink's shape (Phase 1/6) rather than inventing a
    // parallel notification mechanism — same callback signature the
    // engine already uses to notify adapters of Trades/Accepted/etc.
    virtual void on_event(const EngineResponse& response) = 0;

    // Called once at startup (and optionally periodically, e.g. on a
    // timer tick from strategy_runner) to let the strategy decide what
    // to submit next. Strategies are NOT given direct access to book
    // state beyond what on_event provides — no privileged read path,
    // consistent with NFR1 (ordinary EngineAPI caller, same as any
    // adapter).
    virtual void on_tick() {} // default no-op; MomentumStrategy may not need it beyond on_event

protected:
    // Injected at construction, not a global — matches the project's
    // existing constructor-DI convention (Phase 5 TCP server port,
    // Phase 8 RiskConfig).
    EngineAPI& engine_;
};
```

- `on_event` is the single notification path — both strategies derive
  their next action from engine responses (fills, trades, rejects),
  never from polling book state directly. This is what makes NFR1
  concrete: there is no side-channel a `Strategy` could use to see
  more than an adapter would.
- Order submission goes through the same injected `EngineAPI&` every
  adapter uses — `strategy_runner` constructs the engine (or, if
  Phase 8 has landed, the `RiskEngine`-wrapped engine) and passes it
  to the strategy, exactly like the TCP gateway is constructed against
  `EngineAPI` today.
- If Phase 8's `ClientId` retrofit has landed by the time this is
  implemented, `strategy_runner` assigns each strategy instance a
  fixed `ClientId` at construction (a strategy is a single persistent
  "client" for the session) and threads it through order submission
  the same way the TCP/FIX adapters do.

## 3. `MarketMakerStrategy` (R2)

```cpp
struct MarketMakerConfig {
    Price reference_price;   // could migrate to a live reference like
                              // Phase 8's Q4 resolution, or stay static
                              // for this phase — see Open Items below
    Price spread;             // half-spread; bid = ref - spread, ask = ref + spread
    Quantity quote_size;
};
```

- On startup (`on_tick` or an explicit `start()`), submits a bid at
  `reference_price - spread` and an ask at `reference_price + spread`,
  both `LimitOrder`s of `quote_size`.
- On `on_event`, watches for a `Trade` or fill-style event affecting
  one of its own resting orders (matched by `OrderId` it tracks
  internally — the strategy keeps a small map of its own live quote
  `OrderId`s, since `EngineAPI` doesn't hand back ownership tracking
  for free).
- On a fill: cancel the *other* still-resting quote (optional —
  see Open Items, since some market-making designs re-quote both sides
  on any fill, others only replace the filled side), then resubmit
  fresh bid/ask at the current `reference_price ± spread`.
- Two sequential `EngineAPI` calls per re-quote (`cancel` then
  `submit`), per Q2 — no atomicity assumed or needed between them at
  this scope (a competitor could theoretically trade through the brief
  gap; explicitly acceptable per the Charter's non-goal of this not
  being a profit-seeking exercise).

## 4. `MomentumStrategy` (R3)

```cpp
struct MomentumConfig {
    size_t lookback_n;    // how many recent trades feed the signal
    Quantity order_size;
    Price signal_threshold; // minimum price delta to act on
};
```

- Maintains a small ring buffer (`lookback_n` entries) of recent trade
  prices, updated on each `Trade` event via `on_event`.
- Signal: `recent_price - oldest_price_in_buffer`. If the delta
  exceeds `signal_threshold` in either direction, submit a
  `MarketOrder` (simplest — no price to compute, matches "signal can be
  intentionally naive" per R3) in the direction of the delta, sized
  `order_size`.
- No re-quote/cancel logic needed — momentum here is a fire-and-forget
  directional bet per signal, not a resting-order strategy. Simpler
  state machine than the market maker by design.

## 5. `apps/strategy_runner/` (R4, Q3)

- Constructs the engine (`MatchingEngine`, optionally wrapped in
  `RiskEngine` if Phase 8 has landed — same `EngineAPI` either way).
- Constructs one or more `Strategy` instances (config-selected: market
  maker only, momentum only, or both running concurrently against the
  same engine to generate more realistic mixed flow).
- Drives `on_tick()` on a simple loop or timer (exact cadence is an
  Open Item below), and wires the engine's response stream to each
  strategy's `on_event`.
- Runs for a configurable duration or event count (needed for the DoD
  "extended synthetic session" requirement) and then exits cleanly,
  optionally dumping a summary (fills, rejects, final book state) —
  useful both for the DoD write-up and for R5's benchmarking feedback
  loop.

## 6. Feeding back into Phase 2 benchmarking (R5)

- `strategy_runner`'s output (a log or event trace of submitted
  orders) becomes an alternative workload input for Phase 2's
  benchmark harness, replacing or supplementing Phase 2's original
  synthetic generator.
- This likely means `strategy_runner` needs an output format
  compatible with whatever Phase 2's harness already consumes (or
  Phase 2's harness needs a small adapter to consume strategy-runner
  output) — flagged as an Open Item since it depends on Phase 2's
  actual current input format, not something this design can assume
  without checking.

## 7. Definition of Done write-up (per requirements §4)

- A short markdown note (`strategy/README.md` or similar) explicitly
  stating: these strategies are not profit-seeking, `MarketMakerStrategy`
  approximates a passive liquidity-providing participant generating
  continuous two-sided quote traffic, `MomentumStrategy` approximates a
  reactive directional participant generating trend-following order
  bursts — together giving the engine a more realistic mix than a
  single uniform-random order generator would.

## 8. Open items carried into tasks.md

- Confirm whether `MarketMakerStrategy`'s `reference_price` should be
  static (config-only, like Phase 8 Q4's cold-start seed) or update
  from observed trades the same way Phase 8's risk-engine reference
  price does — consistency between the two would be nice but isn't
  required.
- Confirm on-fill behavior: replace only the filled side, or cancel
  and replace both sides on any fill? Both are common real
  market-making behaviors; pick whichever produces more "interesting"
  synthetic flow per this phase's actual goal.
- Confirm `on_tick()` cadence in `strategy_runner` (fixed interval?
  driven by engine event count, mirroring Phase 6's message-count-based
  snapshot cadence for consistency?).
- Check Phase 2's actual benchmark harness input format before
  finalizing `strategy_runner`'s output format (R5) — this design
  assumes compatibility is achievable but hasn't verified the concrete
  shape Phase 2 expects.
