# Phase 9 — Requirements: Minimal FIX Parser

Status: **APPROVED** — Open Questions resolved below; `design.md` and
`tasks.md` are built on this version and the phase is implemented and
test-verified (`fix_adapter_test` = 31 tests / 6 suites passing).

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

## 5. Open Questions — Resolved

1. **ClOrdID → OrderId mapping** — **Numeric-only.** `ClOrdID` (tag
   `11`) is parsed as a `uint64_t` literal; non-numeric values are
   rejected with `InvalidClOrdIdFormat`. Simplest option, consistent
   with the stated non-goal of full FIX spec coverage. See design.md
   §0/§3 for the full resolution.
2. **FIX version** — **FIX 4.2.** At this minimal scope (3 message
   types, no repeating groups) the practical difference from 4.4 is
   small; the header tag list (`8`/`9`/`35`/`49`/`56`/`34`/`52`) in
   design.md §4 assumes 4.2 semantics throughout.
3. **Cancel-request correlation** — **Falls out of Q1 for free.**
   Since ClOrdID is numeric-only and equals `OrderId` directly, `41`
   (OrigClOrdID) on a `35=F` message is parsed the same way and used
   directly as the `cancel()` argument, preferring `41` when present
   and falling back to `11` otherwise. No separate ClOrdID↔OrderId
   tracking table is needed — the adapter is stateless with respect to
   correlation. See design.md §0/§3.
