# Phase 8 — Requirements: Risk Engine

Status: **RESOLVED — open questions settled below; approve this before `design.md`**

## 1. Scope

A pre-trade risk-checking layer that rejects orders violating
configurable rules, before they ever reach `MatchingEngine::submit`.
This is also where everything explicitly deferred out of Phase 1 lands:
tick-size validation (Phase 1 requirements.md, deferred by design) and
self-trade prevention (Phase 1 R14, explicitly "no STP in Phase 1, this
is a Phase 8 concern").

**Cross-cutting concern (was flagged in Phase 5, now resolved — see
§5 Q1):** self-trade prevention requires knowing which client submitted
which order. `ClientId` exists (introduced in Phase 5,
`core/Types.hpp:29-42`) but only reaches the transport layer today, not
the engine's order data. R5 therefore requires a retrofit within this
phase to thread owner identity down to the resting `Order`.

## 2. Functional Requirements (EARS)

- R1: THE RISK ENGINE SHALL wrap `MatchingEngine` behind the same
  `EngineAPI` port (Decorator pattern — explicitly one of the small,
  earned set of patterns from the Charter/steering, not introduced
  speculatively) so that adapters depend on `EngineAPI` and don't need
  to know whether risk checks are enabled.
- R2: WHEN an order's price is more than a configurable percentage away
  from a reference price, THE RISK ENGINE SHALL reject it (price-band
  check) rather than forwarding it.
- R3: WHEN an order's quantity exceeds a configurable maximum, THE RISK
  ENGINE SHALL reject it (fat-finger check).
- R4: WHEN an order's price is not aligned to a configured tick size,
  THE RISK ENGINE SHALL reject it — this is the tick-size validation
  explicitly deferred from Phase 1.
- R5: WHEN self-trade prevention is enabled and an incoming order would
  cross a resting order from the *same* `ClientId`, THE ENGINE SHALL
  apply the configured policy (default: reject the incoming order;
  optional: cancel the resting order and let the incoming one proceed)
  instead of allowing the match (this is the STP explicitly deferred
  from Phase 1 R14). See §5 Q2 and `design.md` §5 for why this check
  lives in the engine match loop, not the decorator.
- R6: Each rejection reason SHALL map to a distinct `EngineResult`
  value so a client can distinguish "your price is out of band" from
  "tick size is wrong" from "duplicate order ID" etc. New values match
  the existing reason-named, no-prefix convention (`design.md` §6):
  `PriceOutOfBand`, `QuantityTooLarge`, `TickSizeMisaligned`,
  `SelfTradePrevented`.
- R7: Risk rules SHALL be configurable via a config struct passed at
  construction (constructor DI) — not hardcoded constants, not a file
  format (see §5 Q3).

## 3. Non-Functional Requirements

- NFR1: Risk checks SHALL run in the same synchronous call path as
  `submit`/`cancel` (no separate thread/queue) — consistent with
  keeping the engine's single-threaded, deterministic character all
  the way through the input path.
- NFR2: A rejected order SHALL have zero side effects on book state or
  `ever_seen_ids_` (Phase 1 §2.1) — a rejected order was never
  accepted, so it should not consume its `OrderId` for
  lifetime-uniqueness purposes. **Confirmed against implementation:**
  `MatchingEngine::submit_limit` (`engine/matching_engine.cpp:65-79`)
  and `submit_market` (`:115-120`) run the duplicate check and then
  `ever_seen_ids_.insert()` *inside* `submit`, after validation.
  Because the RiskEngine decorator rejects config-check failures before
  delegating to `MatchingEngine::submit`, such an order never reaches
  that insertion — NFR2 holds for free. Constraint: the RiskEngine MUST
  run its checks before delegating and MUST NOT touch `ever_seen_ids_`
  itself. (STP is the exception handled inside the engine — see R5 and
  `design.md` §5 for how RejectIncoming preserves this contract via a
  pre-scan before any mutation.)

## 4. Definition of Done

- Every rule (R2–R5) covered by tests, including boundary cases (price
  exactly at the band edge, quantity exactly at the max, price exactly
  one tick off, self-cross with STP on and off, etc.).
- Reused-ID test: an order rejected by a risk check, then a *successful*
  resubmission of a different order with the same `OrderId` — must be
  accepted, proving no `OrderId` was consumed by the rejection (NFR2).
- Phase 2 benchmark re-run and recorded against baseline, quantifying
  the `Order` struct size change (see §6) on the matching hot path,
  per product.md's "benchmark numbers vs baseline" rule.

## 5. Resolved Open Questions

1. **`ClientId` dependency — RESOLVED (retrofit required).** Phase 5
   introduced a strong-typed `ClientId` (`core/Types.hpp:29-42`), **but
   it only travels as far as the transport layer**: it lives in
   `TaggedCommand`/`TaggedResponse` (`core/TaggedCommand.hpp`) purely
   for response routing, and is dropped when the TCP adapter unwraps
   `TaggedCommand.command` into a `NewOrder` for `EngineAPI::submit`.
   The engine-facing types — `LimitOrder`, `MarketOrder`
   (`core/NewOrder.hpp`) and the resting `Order` (`core/Order.hpp`) —
   carry **no** owner. R5 (STP) requires the engine to compare owners
   of a crossing pair, so this phase retrofits owner identity through
   the input path:
     a. Add `ClientId owner` to `LimitOrder` and `MarketOrder`.
     b. Add `ClientId owner` to the resting `Order` struct (so a resting
        order remembers its owner for the cross check).
     c. Populate the resting order's owner from the submission at
        insertion time in `MatchingEngine`.
     d. Update the TCP adapter's unwrap path to carry
        `TaggedCommand.client` into the order rather than discarding it.
   This is a Phase 8 sub-task, **not** a Phase 5 amendment — Phase 5
   stays approved as-is; this phase extends the data it produces.
   (Note: Phase 5's `design.md:51` still shows the abandoned
   `using ClientId = uint64_t` alias — the shipped code is the strong
   wrapper; that stale line is corrected as a documentation-only task.)

2. **STP policy — RESOLVED.** RejectIncoming is the default;
   CancelResting is a configurable alternative mode. RejectIncoming is
   the conservative default and doesn't foreclose the other behavior.
   The check itself lives in the engine match loop, not the decorator
   (see `design.md` §5 for the cost / duplication / side-effect-ordering
   rationale).

3. **Configuration mechanism — RESOLVED.** A config struct passed at
   construction (constructor DI), matching the Charter's pattern list
   and the precedent set by `TcpServer`'s configurable-port
   constructor. No file format (JSON/YAML) now — deferred unless a
   later phase needs it.

4. **Reference price for the price-band check — RESOLVED.** A static
   reference price is required at construction, seeding R2 so the check
   is always active — including cold-start with an empty book, before
   any trade has occurred. The reference MAY migrate to last-trade-price
   once trades exist (`EngineResponse.trades`, `core/Events.hpp`, makes
   the last trade observable), but the static seed removes the
   undefined-pre-first-trade hole. Silently no-op-ing the band check
   pre-first-trade is explicitly rejected as a debugging-surprise risk.

## 6. Cross-cutting impact: `Order` struct size (benchmark-relevant)

The resting `Order` (`core/Order.hpp`) is currently **exactly 64 bytes =
one cache line** on x86-64:

```
id:8, side:4, (pad:4), price:8, quantity:8, sequence:8,
prev:8, next:8, level:8  = 64
```

Adding `ClientId owner` (an 8-byte-aligned `uint64_t`) forces it to
**72 bytes = two cache lines**. It **cannot** be packed back to 64: even
shrinking `Side` to `uint8_t` and reordering fields, the 8-byte owner
still forces 72. The only way to reclaim a cache line would be
index-based (32-bit) pool links — Phase 3-style — which is explicitly
out of scope for Phase 8. Decision: accept 72 bytes, add a
`static_assert(sizeof(Order) == 72)` to pin the layout intentionally,
and re-run the Phase 2 benchmark (DoD §4) to quantify the hot-path
delta — even if the measured impact is negligible, measuring it is the
point.
