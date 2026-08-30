# Phase 9 — Design: Minimal FIX Parser

Status: **DRAFT — pending your approval before tasks.md is executed**

This design resolves the three open questions from requirements.md as
follows (flagging these as proposed resolutions, not yet confirmed by
you — override anything below before this is treated as final):

- **Q1 (ClOrdID→OrderId):** numeric-only. `ClOrdID` (tag 11) is parsed
  as a `uint64_t` literal; non-numeric values are rejected with a
  specific NFR1-style error (`INVALID_CLORDID_FORMAT` or similar).
  Simplest option, consistent with the stated non-goal of full FIX
  spec coverage.
- **Q2 (FIX version):** targeting **FIX 4.2** semantics for header tags
  (`8`/`9`/`35`/`49`/`56`/`34`/`52`). This is the one pure assumption
  in this design — flag if you want 4.4 instead; at this minimal scope
  (3 message types, no repeating groups) the practical difference is
  small, but the header tag list below assumes 4.2.
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
  and, once Phase 8 lands, that entry point is `RiskEngine`, not
  `MatchingEngine` directly. This adapter should be written against
  `EngineAPI`, not `MatchingEngine`, so it composes with Phase 8
  automatically regardless of build order between Phases 8 and 9.
- `NewOrder` built from a `35=D` message needs an `owner` (`ClientId`)
  if Phase 8's retrofit has landed — same as the TCP adapter, this
  adapter should populate `owner` from whatever session/connection
  identity concept applies to a FIX session (likely `SenderCompID`,
  tag `49`, mapped to a `ClientId` the same way the TCP adapter maps a
  connection to one). Flagging this as a dependency to check at
  implementation time: if Phase 8 lands first, this adapter needs a
  `SenderCompID → ClientId` mapping step; if Phase 9 lands first, that
  mapping can be deferred (stub/default `ClientId`) and retrofitted
  when Phase 8 arrives.

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

## 8. Open items carried into tasks.md

- Confirm FIX 4.2 vs 4.4 (Q2) — currently assumed 4.2; flag if wrong
  before implementation starts, since it affects the header tag list
  in §4.
- Confirm sequencing with Phase 8: does this land before or after the
  ClientId retrofit? Affects whether §6's `SenderCompID → ClientId`
  mapping is built now or stubbed.
- Confirm `SenderCompID`/`TargetCompID` (tags `49`/`56`) placeholder
  values — config constants, or does the "single supported symbol"
  scope also imply a single fixed session identity pair?
