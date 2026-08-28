# Phase 7 — Requirements: Binary Wire Protocol

Status: **APPROVED — open questions resolved, see below; design.md follows**

## 1. Scope

`adapters/binary_protocol/` — a fixed-layout binary encoding for order
submission/cancel messages and responses, replacing (or sitting
alongside) Phase 5's plaintext-over-TCP protocol, plus a JSON
equivalent built solely for the comparison benchmark. The deliverable
is as much the *comparison numbers* (latency, payload size, CPU) as the
protocol itself — per the Charter, this only has value with a
benchmark attached, not as an assertion that "binary is obviously
faster."

## 2. Functional Requirements (EARS)

- R1: THE PROTOCOL SHALL define a fixed-width binary layout for each
  message type needed (`LimitOrder` add, `MarketOrder` add, `Cancel`,
  and the corresponding response/trade notification), with explicit
  field sizes, order, and endianness documented.
- R2: THE ENCODER/DECODER SHALL round-trip every message type without
  loss (property/fuzz-testable: encode then decode returns the
  original value for a wide range of inputs).
- R3: A JSON ENCODER/DECODER SHALL be implemented for the same message
  types, using `nlohmann/json`, solely to produce a fair comparison
  baseline — this is not intended to become a supported production
  wire format.
- R4: Benchmark: encode latency, decode latency, and payload size in
  bytes, binary vs. JSON, for each message type, recorded in the same
  results format as prior phases' benchmarks.
- R5: Phase 5's TCP gateway SHALL be updated to support the binary
  protocol as its primary format, with the existing plaintext protocol
  retained as an explicit debug/dev mode (see resolution below).

## 3. Non-Functional Requirements

- NFR1: Binary encode/decode SHALL involve no heap allocation for
  fixed-size messages (this is part of the point of a fixed binary
  layout — if it still allocates, the comparison against JSON is less
  meaningful).

## 4. Definition of Done

- Round-trip tests pass for both encoders.
- Zero-heap-allocation claim (NFR1) verified by an instrumented
  allocation count in tests, not just asserted.
- Comparison benchmark numbers recorded and written up with an honest
  interpretation (e.g. if JSON's disadvantage is dominated by
  allocation rather than parsing, say so — per the Charter, the
  numbers should be understood, not just reported).

## 5. Open Questions — RESOLVED

1. **JSON library** → `nlohmann/json`, added via CMake `FetchContent`.
   First external dependency beyond test/benchmark tooling — accepted
   as a deliberate, scoped exception, not a precedent for adding
   dependencies freely in later phases.
2. **Endianness** → Network byte order, implemented properly (not
   little-endian-only with a caveat). See design.md §2 for the
   byte-swap approach for the project's wider-than-32-bit domain
   types.
3. **Plaintext fallback** → Kept, as an explicit debug/dev mode
   selected at server startup (not per-connection negotiation — see
   design.md §5 for why). Binary is the primary/default protocol.
