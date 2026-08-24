# Phase 7 — Requirements: Binary Wire Protocol

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

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
  types, using a single well-known library (see Open Questions),
  solely to produce a fair comparison baseline — this is not intended
  to become a supported production wire format.
- R4: Benchmark: encode latency, decode latency, and payload size in
  bytes, binary vs. JSON, for each message type, recorded in the same
  results format as prior phases' benchmarks.
- R5: Phase 5's TCP gateway SHALL be updated to use the binary protocol
  as its primary format (see Open Questions for whether plaintext is
  kept as a fallback/debug mode).

## 3. Non-Functional Requirements

- NFR1: Binary encode/decode SHALL involve no heap allocation for
  fixed-size messages (this is part of the point of a fixed binary
  layout — if it still allocates, the comparison against JSON is less
  meaningful).

## 4. Definition of Done

- Round-trip tests pass for both encoders.
- Comparison benchmark numbers recorded and written up with an honest
  interpretation (e.g. if JSON's disadvantage is dominated by
  allocation rather than parsing, say so — per the Charter, the
  numbers should be understood, not just reported).

## 5. Open Questions (resolve before design.md for this phase)

1. **JSON library** — this is the first phase needing an external
   dependency beyond GoogleTest/Benchmark. `nlohmann/json` is the
   standard choice (header-only, well-known, easy via CMake
   `FetchContent`) — confirm you're fine adding this dependency, since
   the Charter's "no dependencies beyond the locked stack" implicitly
   assumed test/benchmark libraries only until now.
2. **Endianness** — little-endian (matches x86/most dev machines,
   simplest) with an explicit note that a "real" cross-platform
   protocol would need network byte order — or actually implement
   network byte order (`htons`/`htonl`-equivalent) for correctness
   points in the write-up? Leaning toward actually doing it correctly
   given how cheap it is, but flagging since it's extra work for a
   single-machine dev/demo setup.
3. **Does plaintext stay as a fallback?** Keeping the Phase 5 plaintext
   parser around as a debug/dev-only mode costs little and is
   genuinely useful for manual testing with `netcat`, but adds a
   little surface area to maintain.
