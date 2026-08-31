# Phase 9 — Design: Minimal FIX Parser

Status: **APPROVED** — `tasks.md` is written from this version and the
phase is implemented and test-verified.

## 0. Resolved Open Questions (from requirements.md)

This design resolves the three open questions from requirements.md as
follows:

- **Q1 (ClOrdID→OrderId):** numeric-only. `ClOrdID` (tag 11) is parsed
  as a `uint64_t` literal; non-numeric values are rejected with a
  specific NFR1-style error (`INVALID_CLORDID_FORMAT` or similar).
  Simplest option, consistent with the stated non-goal of full FIX
  spec coverage.
- **Q2 (FIX version):** targeting **FIX 4.2** semantics for header tags
  (`8`/`9`/`35`/`49`/`56`/`34`/`52`), confirmed via tasks.md T0. At
  this minimal scope (3 message types, no repeating groups) the
  practical difference from 4.4 is small, but the header tag list
  below assumes 4.2 throughout.
- **Q3 (cancel correlation):** falls out of Q1 for free. Since ClOrdID
  is numeric-only and equals `OrderId` directly, tag `41`
  (OrigClOrdID) on a `35=F` message is parsed the same way and used
  **directly** as the `OrderId` argument to `cancel()`. No separate
  ClOrdID↔OrderId tracking table is needed — the adapter is stateless
  with respect to correlation.

---

## 1. Module layout

```
adapters/fix/
  FixMessage.hpp/.cpp   — tag=value tokenizer, SOH splitting, checksum/bodylength
  FixParser.hpp/.cpp    — 35=D / 35=F -> NewOrder / cancel request
  FixEncoder.hpp/.cpp   — EngineResponse/Trade -> 35=8 ExecutionReport
  FixError.hpp          — error/reason type for NFR1
```

Kept as a standalone adapter directory, same shape as the TCP adapter
from Phase 5 — this is a new wire format, not a new transport, so it
sits alongside (not inside) the TCP gateway code.

## 2. Message tokenizing (`FixMessage`)

- Input: raw byte buffer (SOH-delimited, `0x01` separator per FIX
  spec — not a printable "|", which is only used in human-readable
  logging/debug dumps).
- `FixMessage::parse_raw(bytes) -> variant<TagMap, FixError>` — splits
  on SOH, splits each field on `=`, builds a `tag(int) -> value(string
  view)` map. Does **not** interpret tag semantics — that's the
  parser's job. This separation keeps checksum/bodylength validation
  (which is purely mechanical) independent of business-tag mapping.
- Checksum (tag `10`) and BodyLength (tag `9`) are validated here,
  before any business tags are read — a malformed envelope should
  never reach message-type-specific parsing. Both feed NFR1's
  "specific, loggable reason" requirement: `CHECKSUM_MISMATCH`,
  `BODYLENGTH_MISMATCH` are distinct error values, not a generic parse
  failure.

## 3. Parsing (`FixParser`)

- `FixParser::parse(TagMap) -> variant<NewOrder, CancelRequest, FixError>`
- Dispatches on tag `35`:
  - `35=D` → R2 mapping:
    - `11` → `OrderId` (numeric parse per Q1; error
      `INVALID_CLORDID_FORMAT` on failure)
    - `55` → validated against the single supported symbol (Phase 1
      scope); mismatch is a distinct error (`UNSUPPORTED_SYMBOL`), not
      silently ignored, since R1 requires clean rejection rather than
      silent handling for anything out of scope
    - `54` → `Side` (`1`=Buy, `2`=Sell per FIX 4.2; anything else →
      `INVALID_SIDE`)
    - `40` → `OrdType` chooses `LimitOrder` (`2`) vs `MarketOrder`
      (`1`); anything else → `UNSUPPORTED_ORDTYPE` (explicit reject,
      not silent — matches R1's "other types rejected cleanly" spirit
      even though R1 is phrased about message types, not OrdType
      values; applying the same rigor here)
    - `44` → `Price`, required and parsed only when `OrdType` selected
      `LimitOrder`; missing `44` on a Limit order → `MISSING_PRICE`
    - `38` → `Quantity`; missing or non-positive → `INVALID_QUANTITY`
  - `35=F` → R3 mapping:
    - `41` (OrigClOrdID) parsed numerically → `OrderId` to cancel, per
      Q3 resolution above. If `41` is absent, fall back to `11`
      (ClOrdID) as the Open Questions doc allowed — but prefer `41`
      when both are present, since `41` is the more spec-correct field
      for referencing the original order.
  - Any other `35` value → `UNSUPPORTED_MESSAGE_TYPE`, rejected per R1
    (message parsing returns a clean error variant, never throws,
    never silently drops the message).

## 4. Encoding (`FixEncoder`)

- `FixEncoder::encode_execution_report(EngineResponse) -> string`
  (raw SOH-delimited bytes)
- Tag set for `35=8` (minimal, per R4 — not exhaustive FIX 4.x):
  - `37` (OrderID) — from the engine's `OrderId`
  - `11` (ClOrdID) — echoed back as the numeric string form of
    `OrderId` (consistent with Q1: since ClOrdID and OrderId are the
    same value in this adapter, round-tripping it back is free)
  - `17` (ExecID) — a monotonically increasing counter or hash;
    simplest is a per-encoder incrementing counter, since nothing in
    scope requires ExecID to be globally unique across restarts
  - `39` (OrdStatus) — mapped from `EngineResponse`/`Trade` state:
    `0`=New (accepted, unfilled), `1`=PartiallyFilled, `2`=Filled,
    `4`=Cancelled, `8`=Rejected
  - `150` (ExecType) — mirrors `OrdStatus` for this minimal scope
    (real FIX allows these to diverge, e.g. ExecType=Trade with
    OrdStatus=PartiallyFilled; not needed for 3-message-type scope)
  - `55`, `54` — echoed from the originating order for convenience,
    not strictly required by the tag set in R4 but cheap to include
    and helpful for the round-trip DoD test
- Standard 4.2 header wraps all of this: `8` (BeginString=FIX.4.2),
  `9` (BodyLength, computed), `35=8`, `49`/`56` (SenderCompID/
  TargetCompID — can be fixed placeholder strings for this minimal
  scope, e.g. config-supplied constants), `34` (MsgSeqNum, per-session
  counter), `52` (SendingTime, current UTC timestamp) — then the
  business tags above, then `10` (CheckSum, computed last per spec:
  sum of all bytes before `10=` mod 256).

## 5. Error type (NFR1)

```cpp
enum class FixErrorReason {
    ChecksumMismatch,
    BodyLengthMismatch,
    InvalidClOrdIdFormat,
    UnsupportedSymbol,
    InvalidSide,
    UnsupportedOrdType,
    MissingPrice,
    InvalidQuantity,
    UnsupportedMessageType,
    MissingRequiredTag,   // generic fallback naming which tag via a field
};

struct FixError {
    FixErrorReason reason;
    int offending_tag;     // -1 if not tag-specific (e.g. ChecksumMismatch)
    std::string detail;    // human-readable, loggable
};
```

Every rejection path in §3 and the envelope checks in §2 produces one
of these rather than a bool or generic exception — directly satisfies
NFR1's "specific, loggable reason" requirement.

## 6. Where this plugs in

- Parser output (`NewOrder` / cancel `OrderId`) feeds into the same
  `EngineAPI::submit`/`cancel` entry point used by the TCP adapter —
  and, since Phase 8 landed first, that entry point is `RiskEngine` in
  the full server, not `MatchingEngine` directly. This adapter is
  written against `EngineAPI`, not `MatchingEngine`, so it composes
  with Phase 8 transparently.
- **Resolved (T1's finding): Phase 8 landed before Phase 9,** so this
  adapter builds the real `SenderCompID → ClientId` mapping rather than
  a stub. `FixSession` (`adapters/fix/FixSession.hpp/.cpp`) is the
  composition point: it's constructed with an `EngineAPI&`, a fixed
  `ClientId`, and the session's own/peer CompID strings, and stamps
  that `ClientId` as `owner` on every `NewOrder` it submits — the same
  role the TCP adapter's per-connection `ClientId` plays. The design's
  stub contingency for the reverse build order turned out to be
  unnecessary.

## 7. Testing approach

- Unit tests for `FixMessage` tokenizing: valid message, corrupted
  checksum, corrupted bodylength, missing SOH-terminated final field.
- Unit tests for `FixParser`: one test per error reason in §5 (each
  malformed input path), plus valid `35=D` Limit, valid `35=D` Market,
  valid `35=F` with `41` present, valid `35=F` with only `11` present
  (fallback path).
- Round-trip test per DoD: construct a `NewOrder`, build a `35=D`
  message from it (test-only encoder helper, or hand-built fixture
  bytes), parse it back, assert the resulting `NewOrder` matches.
- `FixEncoder` tests: given fixture `EngineResponse`/`Trade` data for
  each of New/PartiallyFilled/Filled/Cancelled/Rejected, assert the
  resulting `35=8` message has correct tag values, correct BodyLength,
  correct CheckSum (computed independently in the test, not by
  reusing the encoder's own checksum routine, to actually catch a
  checksum bug rather than confirm it round-trips with itself).

## 8. Open items — resolved

- FIX 4.2 vs 4.4 (Q2) — **resolved: 4.2** (tasks.md T0).
- Sequencing with Phase 8 — **resolved: Phase 8 landed first**
  (tasks.md T1), so §6 builds the real `SenderCompID → ClientId`
  mapping via `FixSession`, not a stub.
- `SenderCompID`/`TargetCompID` (tags `49`/`56`) — **resolved: caller-
  supplied config strings**, passed to `FixSession`'s constructor
  (`our_comp_id`/`peer_comp_id`) rather than hardcoded constants. Per
  the Charter's non-goal of a production-grade gateway, `FixSession` is
  a complete, independently-tested library (`tests/fix_adapter_test.cpp`)
  but — like the rest of Phase 9's scope — is not wired into a live
  socket listener in `apps/exchange_server/`; that would mean adding a
  FIX transport alongside Phase 5's TCP gateway, which is a new-scope
  decision belonging to its own future phase, not implied by this
  phase's Definition of Done (round-trip + `EngineResponse` encoding
  correctness, verified by tests).
