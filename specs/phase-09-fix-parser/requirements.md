# Phase 9 — Requirements: Minimal FIX Parser

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

## 1. Scope

`adapters/fix/` — a minimal FIX parser/encoder covering exactly three
message types: `35=D` (NewOrderSingle), `35=F` (OrderCancelRequest),
`35=8` (ExecutionReport). Per the Charter's non-goals: this is enough
to demonstrate understanding of the protocol, explicitly not a
production-grade or spec-complete FIX engine.

## 2. Functional Requirements (EARS)

- R1: THE PARSER SHALL parse SOH-delimited tag=value FIX messages for
  `35=D` and `35=F` only; other message types SHALL be rejected
  cleanly (not silently ignored, not crashing) with a clear error.
- R2: THE PARSER SHALL map the following tags from `35=D` to a
  `NewOrder` (Phase 1 §2.2): `11` (ClOrdID) → `OrderId`, `55` (Symbol)
  — ignored/validated against the single supported symbol per Phase 1
  scope, `54` (Side) → `Side`, `44` (Price) → `Price` (Limit only),
  `38` (OrderQty) → `Quantity`, `40` (OrdType) → chooses `LimitOrder`
  vs `MarketOrder` in the `NewOrder` variant.
- R3: THE PARSER SHALL map `35=F`'s `41` (OrigClOrdID) or `11`
  (ClOrdID, per Open Questions) to a cancel request against the
  matching `OrderId`.
- R4: THE ENCODER SHALL generate `35=8` (ExecutionReport) messages from
  `EngineResponse`/`Trade` data, including at minimum: `37` (OrderID),
  `17` (ExecID), `39` (OrdStatus), `150` (ExecType) — full tag set to
  be finalized in design.md, not an exhaustive FIX 4.x implementation.
- R5: THE ENCODER SHALL compute `9` (BodyLength) and `10` (CheckSum)
  correctly per the FIX spec's algorithm for both.

## 3. Non-Functional Requirements

- NFR1: Malformed messages (bad checksum, missing required tag,
  unparseable value) SHALL be rejected with a specific, loggable
  reason — not a generic parse failure, since FIX debugging in
  practice depends heavily on knowing exactly which tag was the
  problem.

## 4. Definition of Done

- Round-trip test: construct a `NewOrder`, encode conceptually as if
  received (test fixtures, not a live counterparty), parse it, and
  confirm the engine receives the correct `NewOrder`.
- ExecutionReport correctly reflects fills/rejects from real
  `EngineResponse`/`Trade` data in tests.

## 5. Open Questions (resolve before design.md for this phase)

1. **ClOrdID → OrderId mapping** — FIX's `ClOrdID` (tag 11) is
   spec'd as a string, but the engine's `OrderId` is a `uint64_t`.
   Parse the string as a numeric literal and reject non-numeric
   ClOrdIDs (simplest, but arguably "not really FIX-compliant" since
   real ClOrdIDs are often alphanumeric)? Or introduce a string↔uint64
   mapping table in the adapter (more faithful to real FIX usage, more
   code for a "minimal" parser)? Leaning toward the numeric-only
   approach given the stated non-goal of full spec coverage, but
   flagging since it's a real compromise, not a free choice.
2. **FIX version** — FIX 4.2 vs 4.4 tag semantics differ in places;
   which are you targeting (matters for exactly which tags are
   "standard" for the header, e.g. `8`/`9`/`35`/`49`/`56`/`34`/`52`)?
3. **Cancel-request correlation** — `35=F` typically references the
   *original* order via `41` (OrigClOrdID), with its own new `11`
   (ClOrdID) for the cancel request itself. Given `MatchingEngine`'s
   `cancel(OrderId)` interface, does this adapter need to track its
   own ClOrdID↔OrderId table, or is collapsing `41`/`11` acceptable for
   this minimal scope?
