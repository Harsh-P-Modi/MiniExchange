# Phase 8 — Requirements: Risk Engine

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

## 1. Scope

A pre-trade risk-checking layer that rejects orders violating
configurable rules, before they ever reach `MatchingEngine::submit`.
This is also where everything explicitly deferred out of Phase 1 lands:
tick-size validation (Phase 1 requirements.md, deferred by design) and
self-trade prevention (Phase 1 R14, explicitly "no STP in Phase 1, this
is a Phase 8 concern").

**⚠️ Same cross-cutting concern flagged in Phase 5:** self-trade
prevention requires knowing which client submitted which order —
`ClientId` needs to exist by this point. If Phase 5 introduced it (as
recommended there), this phase just consumes it. If Phase 5 didn't,
this phase cannot implement R5 below without first retrofitting that
concept — flagging so it isn't discovered mid-phase.

## 2. Functional Requirements (EARS)

- R1: THE RISK ENGINE SHALL wrap `MatchingEngine` behind the same
  `EngineAPI` port (Decorator pattern — explicitly one of the small,
  earned set of patterns from the Charter/steering, not introduced
  speculatively) so that adapters depend on `EngineAPI` and don't need
  to know whether risk checks are enabled.
- R2: WHEN an order's price is more than a configurable percentage away
  from a reference price (e.g. last trade price), THE RISK ENGINE
  SHALL reject it (price-band check) rather than forwarding it.
- R3: WHEN an order's quantity exceeds a configurable maximum, THE RISK
  ENGINE SHALL reject it (fat-finger check).
- R4: WHEN an order's price is not aligned to a configured tick size,
  THE RISK ENGINE SHALL reject it — this is the tick-size validation
  explicitly deferred from Phase 1.
- R5: WHEN self-trade prevention is enabled and an incoming order would
  cross a resting order from the *same* `ClientId`, THE RISK ENGINE
  SHALL apply a configurable policy (reject the incoming order, or
  cancel the resting order — see Open Questions) instead of allowing
  the match (this is the STP explicitly deferred from Phase 1 R14).
- R6: Each rejection reason SHALL map to a distinct `EngineResult`
  value (or an equivalent richer rejection-reason type) so a client can
  distinguish "your price is out of band" from "tick size is wrong"
  from "duplicate order ID" etc.
- R7: Risk rules SHALL be configurable (not hardcoded constants) — see
  Open Questions for the configuration mechanism.

## 3. Non-Functional Requirements

- NFR1: Risk checks SHALL run in the same synchronous call path as
  `submit`/`cancel` (no separate thread/queue) — consistent with
  keeping the engine's single-threaded, deterministic character all
  the way through the input path.
- NFR2: A rejected order SHALL have zero side effects on book state or
  `ever_seen_ids_` (Phase 1 §2.1) — a rejected order was never
  accepted, so it should not consume its `OrderId` for
  lifetime-uniqueness purposes. (Worth double-checking this against
  Phase 1's actual implementation — if `ever_seen_ids_` is populated
  before risk checks run, that's a bug introduced by this phase's
  wrapping, not a new requirement.)

## 4. Definition of Done

- Every rule (R2–R5) covered by tests, including boundary cases (price
  exactly at the band edge, quantity exactly at the max, etc.).
- Confirmed no `OrderId` is consumed for a risk-rejected order.

## 5. Open Questions (resolve before design.md for this phase)

1. **`ClientId` dependency** (flagged above) — confirm whether Phase 5
   already introduced this, or whether it needs to happen as a
   prerequisite step within this phase instead.
2. **STP policy** — reject the incoming order outright, or cancel the
   resting order and let the incoming one proceed (real venues differ
   on this)? Recommend reject-incoming as the simpler default with the
   other mode as a configurable option, but this is a real design
   choice, not an obvious one.
3. **Configuration mechanism** — a simple config struct passed at
   construction (simplest, matches "plain constructor DI" from the
   Charter's pattern list), a config file (JSON/YAML, more "production"
   feeling but adds a dependency/complexity), or both? Leaning toward
   the struct for now, deferring a file format unless you want the
   extra polish.
4. **Reference price for the price-band check** — last trade price
   (simple, but undefined before the first trade ever happens on a
   symbol), a configured static reference, or something else for the
   cold-start case?
