# Phase 8 — Design: Risk Engine

Status: **DRAFT — pending approval before `tasks.md` is executed**

Builds on the resolved `requirements.md`: Q1 (ClientId retrofit), Q2
(STP policy + placement), Q3 (config struct), Q4 (static reference
price) are settled. NFR2's `ever_seen_ids_` ordering is **confirmed**
against `engine/matching_engine.cpp` (see §7) — no longer an assumption.

---

## 1. Prerequisite: ClientId retrofit

Before self-trade prevention can work, `ClientId` has to reach the
engine's order data. Today it dies at the TCP adapter boundary
(`TaggedCommand` unwrap into a `NewOrder`). This is a **sub-task of
Phase 8, not a Phase 5 amendment** — Phase 5 stays approved as-is; this
phase extends the data it produces.

Changes required:

- **`core/NewOrder.hpp`** — `LimitOrder` and `MarketOrder` each gain a
  `ClientId owner` field.
- **`core/Order.hpp`** — the resting `Order` struct gains a
  `ClientId owner` field, populated when an order rests on the book.
  This is the invasive change: `Order` is currently **exactly 64 bytes
  = one cache line** (`id:8, side:4, pad:4, price:8, quantity:8,
  sequence:8, prev:8, next:8, level:8`). Adding an 8-byte-aligned
  `owner` forces it to **72 bytes = two cache lines**. It **cannot** be
  packed back to 64 — even shrinking `Side` to `uint8_t` and reordering,
  the 8-byte owner forces 72; only Phase 3-style 32-bit index pool links
  could reclaim a line, which is out of scope here. Pin the layout with
  `static_assert(sizeof(Order) == 72)` so any future accidental growth
  is caught at compile time, and re-run the Phase 2 benchmark to
  quantify the hot-path delta (requirements.md §6, DoD).
- **TCP adapter** — stops discarding `TaggedCommand.client` on unwrap;
  populates `owner` on the `NewOrder` variant before calling
  `EngineAPI::submit`.
- **`EngineAPI::submit`** — signature unchanged; owner rides inside the
  `NewOrder` variant rather than as a second parameter (cleaner given
  it's already a variant, and keeps the port stable).

This retrofit must land and be tested (owner threads end to end,
TCP → engine → resting order) before R5 (STP) is implemented. R2–R4 and
R6–R7 do **not** depend on it and can be built first (see `tasks.md`
sequencing).

## 2. Architecture: RiskEngine as Decorator

```
        EngineAPI (interface, unchanged)
             ▲
   ┌─────────┴─────────┐
   │                    │
MatchingEngine     RiskEngine
                    (wraps EngineAPI, delegates on pass)
```

- `RiskEngine` implements `EngineAPI` and holds an injected
  `EngineAPI* inner` (constructor DI, matching Phase 5's gateway
  convention).
- `submit()` / `cancel()` run the config-driven checks synchronously,
  then either return a rejection `EngineResult` or forward to
  `inner->submit()` / `inner->cancel()`.
- Adapters (TCP gateway) are constructed against `EngineAPI` and never
  know whether they're talking to `MatchingEngine` directly or through
  `RiskEngine` — matches R1 exactly, no adapter changes needed beyond
  the ClientId threading in §1.
- No class hierarchy for the checks: each config rule is a private
  method on `RiskEngine` (`check_price_band`, `check_fat_finger`,
  `check_tick_size`), called in sequence from `submit()`. A
  strategy/rule-object hierarchy would be speculative — there's no
  requirement to swap rules independently at runtime.

**Important boundary:** the decorator owns only the three *stateless,
pre-matching* config checks (price-band, fat-finger, tick-size). STP is
**not** a decorator check — it is a matching-time concern and lives in
the engine (see §5).

## 3. Configuration (Q3)

```cpp
enum class StpPolicy { RejectIncoming, CancelResting };

struct RiskConfig {
    double  price_band_pct;          // e.g. 0.10 for ±10%
    Quantity max_order_qty;          // fat-finger ceiling
    Price   tick_size;               // global (single-symbol, see below)
    Price   initial_reference_price; // Q4 cold-start seed
    bool    stp_enabled;
    StpPolicy stp_policy;            // RejectIncoming (default) | CancelResting
};
```

Passed to `RiskEngine`'s constructor — plain struct DI, matching the
TCP server's existing constructor-configured port. No config file
format introduced.

**Single-symbol.** The project is single-symbol (product.md), so this is
one flat `RiskConfig`, not a per-symbol map. `SymbolId` existing as a
type is not a reason to build `unordered_map<SymbolId, RiskConfig>` now
— that's speculative complexity the Charter's "patterns stay minimal and
earned" rule forbids. If a future phase goes multi-symbol, this can grow
into a per-symbol map without changing the pattern; documented here as a
future option only, not a planned structure.

The STP-related config (`stp_enabled`, `stp_policy`) is passed through to
the engine, since that's where STP executes (§5). The `RiskConfig` is the
single composition-root config object; the engine receives the STP
portion it needs.

## 4. Reference price / cold start (Q4)

- The price-band reference is a single `Price reference_price`, seeded
  from `RiskConfig::initial_reference_price` at construction. Single
  value, not a per-symbol map (single-symbol, per §3).
- Seeding at construction makes the band check **always active** — never
  a silent no-op before the first trade, which is the cold-start hole
  Q4 explicitly rejected.
- Optional migration: on each `Trade` observed in an `EngineResponse`
  returned by `inner->submit()`, `reference_price` MAY be updated to the
  last trade's price, letting the band track the market once trading
  starts. This is a small, self-contained addition; the static seed is
  what guarantees correctness at t=0.

## 5. Self-trade prevention (R5, Q2) — in the engine match loop

STP lives **inside `MatchingEngine`'s matching path**, gated by the STP
config/policy threaded through, **not** in the decorator.

### Why not the decorator (the "why not X")

State all three honestly — this is the main tradeoff of the phase:

1. **It duplicates the engine's crossing logic.** `match_against_book`
   (`engine/matching_engine.cpp`) already walks the opposite-side tree
   from best price up to the incoming limit, in FIFO order per level. A
   decorator doing STP would have to re-implement that same traversal
   just to discover which resting orders the incoming order *would*
   cross.
2. **Cost.** In the decorator, that pre-walk is O(crossable depth) per
   submit (levels × orders scanned); a market order sweeps *every*
   level. Inside the loop, STP is O(1) per examined pair — `owner` is a
   direct field read on the resting `Order` the loop already holds.
3. **Zero-side-effect ordering (the decisive one).** `submit_limit`
   inserts into `ever_seen_ids_` and emits `on_order_accepted`
   **before** `match_against_book` runs. If STP were detected
   mid-matching, it would arrive *after* the ID was consumed, the accept
   event fired, and partial fills already happened — violating NFR2 and
   R5's "instead of allowing the match." So detection has to happen
   before any mutation.

Note this is a **cost + duplication + ordering** argument, not an
impossibility one: the book walk *is* reachable from outside
(`Order::next`, `OrderBook::bids()/asks()`, `PriceLevel::front()` are
all public). A decorator *could* do it — it just shouldn't, for the
three reasons above.

**Accepted tradeoff:** STP logic sits in `engine/` rather than keeping
R1's decorator strictly pure. The decorator stays pure for the three
config checks; the engine gains one matching-time rule. This is the
honest cost of R5, called out rather than hidden.

### How it works

- **RejectIncoming (default): pre-scan before any mutation.** Before
  `submit_limit`/`submit_market` touches `ever_seen_ids_` or emits
  `on_order_accepted`, if STP is enabled, scan the opposite side that
  the order would cross (best price up to the limit) for a resting order
  with the same `owner`. If found, return `SelfTradePrevented`
  immediately — no ID consumed, no events, no fills. This preserves
  NFR2 and R5 exactly. The scan reuses the same crossing predicate the
  match loop uses, kept in one place.
- **CancelResting: inside the loop.** When the match loop reaches a
  resting order whose `owner` equals the incoming order's `owner`,
  remove that resting order (same path as a normal cancel, emitting
  `on_order_cancelled`) and let `level->front()` advance to the next
  order. The incoming order continues matching/resting normally. This
  belongs in the loop because it interleaves with matching rather than
  gating it.

Both modes stay within NFR1's synchronous, single-threaded call path —
no reentrancy, all on one call stack. `tasks.md` includes an explicit
CancelResting ordering test.

## 6. Rejection reasons (R6)

`EngineResult` is a plain `enum class` (`core/Events.hpp`), with existing
values reason-named and **un-prefixed** (`Accepted`, `DuplicateOrderId`,
`UnknownOrderId`, `InvalidQuantity`, `InvalidPrice`, `PoolExhausted`).
Add, matching that convention exactly (no `Rejected*` prefix):

- `PriceOutOfBand`
- `QuantityTooLarge`
- `TickSizeMisaligned`
- `SelfTradePrevented`

## 7. NFR2 — `ever_seen_ids_` ordering (CONFIRMED)

Confirmed by reading `engine/matching_engine.cpp`:

- `submit_limit` (~lines 65-79): validates (zero-qty, non-positive
  price, duplicate ID, pool-exhausted), *then* calls
  `ever_seen_ids_.insert(order.id)`, *then* emits `on_order_accepted`,
  *then* matches.
- `submit_market` (~lines 115-120): validates (zero-qty, duplicate ID),
  *then* `ever_seen_ids_.insert(...)`, then matches.

The insertion is **inside** `submit`, after validation. A
config-check-rejected order intercepted by the `RiskEngine` decorator
never calls `inner->submit()`, so it never reaches the insertion — NFR2
holds with **no extra guard code**, purely from the decorator ordering.

Two constraints that keep this true:
- The `RiskEngine` MUST run its checks before delegating, and MUST NOT
  touch `ever_seen_ids_` itself (it has no reason to).
- There must be exactly one `EngineAPI`-typed entry point in the
  composition root, so nothing bypasses the decorator by calling
  `MatchingEngine::submit` directly. (Architectural expectation, easy to
  hold in a single-composition-root app.)

STP RejectIncoming preserves NFR2 by the pre-scan-before-mutation design
in §5 — the ID is never consumed for a self-trade-rejected order.

## 8. Testing approach

- Unit tests for each decorator check in isolation (`RiskEngine`
  constructed with a stub `EngineAPI` inner, asserting rejection without
  delegation, and pass-through when within limits).
- Boundary cases per DoD: price exactly at band edge (accept), one
  beyond (reject); quantity exactly at max (accept), one over (reject);
  price exactly on tick (accept), one tick-unit off (reject).
- Cold-start: with an empty book and no trades yet, an out-of-band price
  is rejected using the seeded static reference (proves the hole is
  closed).
- STP RejectIncoming: same-owner cross rejected with no ID consumed, no
  events, no fills; different-owner cross proceeds; self-cross with STP
  disabled proceeds.
- STP CancelResting: submit resting order, submit crossing order from
  the same owner, confirm the resting order is cancelled
  (`on_order_cancelled` emitted) and the incoming order proceeds.
- Reused-ID (NFR2): reject an order via a risk check, then resubmit a
  different order with the same `OrderId` → accepted.
- Integration: extend Phase 5/7's gateway fixture — submit through the
  full TCP → RiskEngine → MatchingEngine path, confirm a rejection
  produces the right wire-level response and never appears in the book.

## 9. Definition of Done

- R1–R7 implemented; every rule (R2–R5) covered by tests including the
  boundary and cold-start cases in §8.
- `static_assert(sizeof(Order) == 72)` in place; owner threads end to
  end.
- Reused-ID test passing (NFR2).
- Phase 2 benchmark re-run and recorded against baseline, quantifying
  the `Order` 64→72 byte change on the matching hot path.
- Phase 5's stale `design.md` `ClientId` alias line corrected.
- `docs/LEARNING.md` updated per steering (bundled with the
  implementation tasks, not this spec pass).
