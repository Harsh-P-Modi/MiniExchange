# ADR-007: Risk Engine as a Decorator, with Self-Trade Prevention Inside the Engine

**Status:** Accepted
**Date:** Phase 8

## Context

Phase 8 adds pre-trade risk checks: fat-finger (quantity ceiling),
tick-size (price alignment), price-band (deviation from a reference
price), and self-trade prevention (STP — stop a client from trading
against its own resting orders). All four need to run before an order
is allowed to affect book state, and none of them should require
`engine/`, `orderbook/`, or `core/` to change their existing
public contract (`EngineAPI`), per this project's Ports & Adapters
rule (ADR-004) and the Charter's "the engine performs zero I/O, stays
a pure matching engine" principle.

Two placement questions had to be answered:

1. Where do the checks live — inside `MatchingEngine`, or as a
   separate layer in front of it?
2. Does STP belong with the other three checks, or somewhere else?

## Decision

**The three stateless, pre-matching checks (fat-finger, tick-size,
price-band) live in a new `RiskEngine` class that implements
`EngineAPI` and wraps an injected `EngineAPI* inner` — the Decorator
pattern.** `submit()`/`cancel()` run the config-driven checks
synchronously; on pass, they forward to `inner->submit()`/`cancel()`
unchanged. Adapters (the TCP gateway, the FIX adapter, the Strategy
SDK) are constructed against `EngineAPI` and never know whether
they're talking to a bare `MatchingEngine` or a `RiskEngine`-wrapped
one — no adapter code changes to add risk checks, only composition-root
wiring (`apps/exchange_server/main.cpp` and friends) changes.

**Self-trade prevention is the one risk-adjacent check that does NOT
live in the decorator — it lives inside `MatchingEngine`'s match loop
itself**, gated by config threaded through from `RiskConfig`. See
`specs/phase-08-risk-engine/design.md` §5 for the full worked-through
reasoning; summarized here:

1. **Duplication.** `match_against_book` already walks the opposite-side
   price tree from best price up to the incoming limit, in FIFO order
   per level, to find fills. A decorator-side STP check would have to
   re-walk that same structure just to discover which resting orders the
   incoming order *would* cross — the same traversal, done twice.
2. **Cost.** Re-walking from outside the loop is O(crossable depth) per
   submit (a market order sweeps every level it can reach). Detecting
   the same condition from inside the loop, where the traversal is
   already happening, is O(1) per examined pair — `owner` is a field
   read on the resting `Order` the loop already holds.
3. **Zero-side-effect ordering — the decisive reason.**
   `submit_limit`/`submit_market` insert the new `OrderId` into
   `ever_seen_ids_` and emit `on_order_accepted` **before**
   `match_against_book` runs (this ordering is itself covered by NFR2 —
   see `specs/phase-08-risk-engine/design.md` §7). A decorator sits
   *outside* `submit()` entirely, so by the time it could inspect
   anything, the ID would already be consumed and the accept event
   already fired — an STP rejection discovered there would arrive too
   late to satisfy R5's "prevent the match," and would violate NFR2's
   "a rejected order never touches book state" guarantee. Detection has
   to happen *before* any mutation, which means inside the same call
   that's about to mutate — not in a layer that only runs before that
   call starts.

## Alternatives Considered

- **STP as a fourth decorator check, pre-scanning the book from
  outside.** Rejected: technically possible (the book's public
  traversal — `Order::next`, `OrderBook::bids()`/`asks()`,
  `PriceLevel::front()` — is reachable from outside `engine/`), but it
  fails on cost (duplicated O(depth) walk) and, decisively, on ordering
  (see reason 3 above). This is a cost + duplication + ordering
  argument, not an impossibility argument — a decorator-side STP check
  *could* be made correct with enough extra bookkeeping (e.g. deferring
  `on_order_accepted` until after an external pre-scan), but that would
  mean re-deriving inside the decorator exactly the sequencing
  `MatchingEngine` already gets for free, for no benefit.
- **A rule-object/Strategy hierarchy for the four checks**, so checks
  could be added/removed/reordered at runtime. Rejected as speculative:
  nothing in this project's scope requires swapping risk rules at
  runtime, and the Charter's "design patterns stay minimal and earned"
  rule explicitly calls out Strategy as deferred until an actual need
  exists (Phase 10 is where Strategy finally earns its place, for
  synthetic order-flow generators — a different problem entirely). Each
  check is instead a private method on `RiskEngine`
  (`check_price_band`, `check_fat_finger`, `check_tick_size`), called
  in sequence.
- **Splitting `RiskEngine` into per-symbol instances up front** (since
  `SymbolId` already exists as of Phase 6). Rejected: the project is
  single-symbol (`.kiro/steering/product.md`), so one flat `RiskConfig`
  is correct today; building `unordered_map<SymbolId, RiskConfig>` now
  would be complexity paid for before it's earned. Documented as a
  future option, not a planned structure.

## Consequences

- **Benefit:** `MatchingEngine` stays a pure matching engine — risk is
  an opt-in layer, not a fork. A caller that wants no risk checks at
  all constructs `MatchingEngine` directly and never touches
  `RiskEngine`; the FIX adapter and Strategy SDK (Phases 9–10) compose
  with `RiskEngine` automatically, for free, simply by being written
  against `EngineAPI` rather than `MatchingEngine` — no phase-9/10-side
  work was needed to get risk checks for those clients.
- **Benefit:** Each rejection maps to a distinct `EngineResult`, and
  because the decorator checks *before* delegating, a rejected order
  (from any of the three decorator checks) consumes no `OrderId` and
  has zero effect on book state — the same guarantee STP's ordering
  argument depends on, now true for the decorator's checks by
  construction (they run strictly before `inner->submit()`).
- **Drawback / accepted cost:** the Decorator boundary is not
  perfectly pure — `RiskEngine` stays limited to the three genuinely
  stateless, pre-matching checks, while STP is a documented exception
  living in `engine/` instead. This is called out explicitly rather
  than papered over: "the decorator owns only the three stateless,
  pre-matching config checks... STP is not a decorator check" (design.md
  §2). A reader auditing "is this a clean Decorator" should find this
  ADR and the design doc's §5 before concluding it's a modeling
  mistake — it's a deliberate, reasoned exception.
- **Drawback:** `MatchingEngine` now has one config-gated behavior
  (STP) that isn't purely "matching logic" in the narrowest sense —
  it's a risk rule that happens to require matching-time information.
  Accepted because the alternative (duplicating the crossing walk, or
  restructuring event emission order) costs more than it buys.

## See Also

- `specs/phase-08-risk-engine/design.md` §2 (Decorator architecture),
  §5 (STP placement, full reasoning), §7 (NFR2 ordering confirmation).
- ADR-004 (Ports & Adapters) — this decision is downstream of that one:
  `RiskEngine` implementing `EngineAPI` is what lets it be a drop-in
  decorator at all.
- ADR-006 (ClientId) — STP's `owner` comparison depends on the
  `ClientId` retrofit ADR-006 describes.
