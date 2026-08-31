# Phase 9 — Tasks: Minimal FIX Parser

Status: **COMPLETE** — all tasks implemented and test-verified.

## Completion summary

- **All tasks (T0–T14) done and verified.** `fix_adapter_test` =
  **31 tests / 6 suites passing**; builds clean under `-Werror`.
- **T0/T1 [VERIFY] resolved:** FIX 4.2 confirmed; Phase 8 has landed, so
  T8 built the **real** `SenderCompID → ClientId` mapping (via
  `FixSession`), not a stub — the design's stub contingency was
  unnecessary.
- **Files:** `adapters/fix/{FixError.hpp, FixMessage.hpp/.cpp,
  FixParser.hpp/.cpp, FixEncoder.hpp/.cpp, FixSession.hpp/.cpp}`,
  `tests/fix_adapter_test.cpp`; `adapters_fix` library + `fix_adapter_test`
  wired in `CMakeLists.txt` (outside the UNIX-only block — a wire format,
  not a transport, so platform-neutral).
- **`docs/LEARNING.md`:** Phase 9 section written (SOH framing, the exact
  CheckSum/BodyLength algorithms, the numeric-ClOrdID compromise and how
  it collapses Q3, the OrdStatus mapping, and the `EngineAPI`-composition
  with Phase 8's RiskEngine).
- The FIX adapter is written against `EngineAPI`, so it composes with the
  Phase 8 `RiskEngine` decorator automatically — a FIX order gets the same
  risk checks as a TCP order.

Ordered so each task is independently testable. Tasks marked
**[VERIFY]** are quick confirmations that should happen before their
dependent implementation task.

---

- [x] **T0 [VERIFY]** — Confirm FIX 4.2 vs 4.4 targeting (design.md's
      Q2 assumption). No code; just settles the header tag list before
      T5 below is implemented against a specific version's semantics.

- [x] **T1 [VERIFY]** — Confirm sequencing relative to Phase 8: has the
      ClientId retrofit (Phase 8, T2–T4) landed yet? Determines whether
      T8 below builds the real `SenderCompID → ClientId` mapping or a
      stub.

- [x] **T2** — `FixError` / `FixErrorReason` type (design.md §5). No
      dependencies; needed by every later task.

- [x] **T3** — `FixMessage`: SOH tokenizer + tag map builder. Unit
      tests: valid message tokenizes correctly, missing final SOH
      handled, empty message handled.

- [x] **T4** — `FixMessage`: checksum (tag `10`) and BodyLength (tag
      `9`) validation, run before business-tag parsing. Unit tests:
      correct checksum passes, corrupted checksum → `ChecksumMismatch`,
      corrupted bodylength → `BodyLengthMismatch`.

- [x] **T5** — `FixParser`: `35=D` (NewOrderSingle) mapping per
      design.md §3 — all of tags `11`/`55`/`54`/`40`/`44`/`38`, both
      Limit and Market `OrdType` branches. One test per error reason
      this path can produce (`InvalidClOrdIdFormat`,
      `UnsupportedSymbol`, `InvalidSide`, `UnsupportedOrdType`,
      `MissingPrice`, `InvalidQuantity`), plus valid-Limit and
      valid-Market happy-path tests.

- [x] **T6** — `FixParser`: `35=F` (OrderCancelRequest) mapping per
      design.md §3 / Q3 resolution — `41` preferred, `11` fallback.
      Tests: `41` present (used), `41` absent/`11` present (fallback
      used), both absent (`MissingRequiredTag`).

- [x] **T7** — `FixParser`: reject-cleanly path for any other `35`
      value (`UnsupportedMessageType`) — satisfies R1's "not silently
      ignored" requirement explicitly, with its own test.

- [x] **T8** — Session identity mapping: `SenderCompID` (tag `49`) →
      `ClientId`, gated on T1's finding. If Phase 8 has landed, wire
      the real mapping and populate `owner` on parsed `NewOrder`s; if
      not, stub with a fixed default `ClientId` and leave a clear
      `// TODO(Phase 8 retrofit)` marker plus a tracked follow-up task
      for whichever phase lands second.

- [x] **T9** — `FixEncoder`: header construction (tags
      `8`/`9`/`35`/`49`/`56`/`34`/`52`) with correct BodyLength
      computed before CheckSum, per design.md §4. Confirm
      `49`/`56` placeholder source (design.md's carried-open-item) —
      config constants unless T0/design review says otherwise.

- [x] **T10** — `FixEncoder`: business tags for `35=8`
      (`37`/`11`/`17`/`39`/`150`, plus `55`/`54` echo) mapping from
      `EngineResponse`/`Trade` per design.md §4's `OrdStatus`/`ExecType`
      table. One test per status: New, PartiallyFilled, Filled,
      Cancelled, Rejected.

- [x] **T11** — `FixEncoder`: CheckSum computation, tested
      independently (test computes the expected checksum itself rather
      than reusing the encoder's routine) per design.md §7's note —
      this is the test that actually catches a checksum bug rather
      than just confirming self-consistency.

- [x] **T12** — Round-trip test (DoD): construct `NewOrder` →
      build/parse `35=D` fixture → confirm resulting `NewOrder` matches
      the original, for both Limit and Market variants.

- [x] **T13** — Wire the parser/encoder into the actual `EngineAPI`
      entry point (design.md §6) — parsed orders go to
      `EngineAPI::submit`/`cancel`, `EngineResponse` results go to
      `FixEncoder`. Written against `EngineAPI`, not `MatchingEngine`
      directly, so it composes with Phase 8 regardless of landing
      order.

- [x] **T14** — Integration test: full parse → submit → response →
      encode loop using fixture `EngineResponse`/`Trade` data, per DoD's
      "ExecutionReport correctly reflects fills/rejects" requirement.

---

## Sequencing notes

- T0–T1 first — cheap, de-risk the rest.
- T2–T7 (tokenizing, parsing, error type) have no Phase 8 dependency
  and can proceed regardless of T1's answer.
- T8 is the only task whose *implementation* (not existence) depends
  on Phase 8's landing order — it exists either way, just with a stub
  or the real mapping.
- T9–T12 (encoder side) are independent of Phase 8 entirely.
- T13–T14 are integration and come last.
