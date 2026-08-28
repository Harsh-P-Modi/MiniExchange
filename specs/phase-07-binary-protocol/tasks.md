# Phase 7 — Tasks: Binary Wire Protocol

Status: **DRAFT — pending approval**

Each task lists the requirements/design sections it satisfies. Tasks
that produce or meaningfully change code end with a LEARNING.md note
per the steering policy.

- [ ] **1. Vendor `nlohmann/json` via CMake `FetchContent`**
  Add the dependency to the top-level `CMakeLists.txt`, pinned to a
  specific released version (not a floating branch). Confirm it only
  links into the JSON codec and benchmark targets, not into `core/` or
  `engine/`.
  _Satisfies: design.md §0 row 1_
  _LEARNING.md: note this is the project's first non-test/benchmark
  external dependency, and why it's scoped to stay out of `core/`/`engine/`._

- [ ] **2. Define message structs and `MessageType` (`adapters/binary_protocol/Message.hpp`)**
  Implement the six structs (`LimitOrderAddMsg`, `MarketOrderAddMsg`,
  `CancelMsg`, `AckMsg`, `RejectMsg`, `TradeNotificationMsg`) exactly
  per design.md §2, reusing `core/` strong types. No logic — pure data
  layout, mirroring Phase 6's `FeedMessage.hpp` approach.
  _Satisfies: R1, design.md §2_

- [ ] **3. Byte-order helpers (`adapters/binary_protocol/ByteOrder.hpp`)**
  Implement `to_network`/`from_network` for raw 16/32/64-bit integers
  (`htons`/`htonl`/`htobe64` and their inverses) and the generic
  templated overload for `core/`'s strong-typed wrappers, per design.md
  §2. Unit tests: round-trip a raw value through `to_network` then
  `from_network` for each width, confirm no-op on a platform where
  host and network order coincide is still correctly swapped-and-
  unswapped (i.e., test the swap logic itself, not just idempotence).
  _Satisfies: R1 (endianness), design.md §2_

- [ ] **4. `encode`/`decode` for a single message type (`LimitOrderAddMsg`)**
  Implement `encode<LimitOrderAddMsg>` and the `LimitOrderAdd` branch
  of `decode` per design.md §3 — write/read every field through the
  byte-order helpers from task 3 into/from a caller-provided
  `std::span<std::byte>`. Establish the pattern the remaining five
  types (task 5) repeat.
  _Satisfies: R1, R2, design.md §3_

- [ ] **5. Extend `encode`/`decode` to the remaining five message types**
  `MarketOrderAddMsg`, `CancelMsg`, `AckMsg`, `RejectMsg`,
  `TradeNotificationMsg` — same pattern as task 4, dispatched through
  `AnyMessage` (`std::variant`) in `decode`.
  _Satisfies: R1, R2, design.md §3_

- [ ] **6. Round-trip property tests for the binary codec (R2)**
  For each of the six message types, generate a wide range of field
  values (boundary values, at minimum: 0, max representable, and a
  handful of typical values) and assert `decode(encode(msg)) == msg`.
  Include at least one case per type exercising every field, not just
  the first one.
  _Satisfies: R2, Definition of Done ("round-trip tests pass")_

- [ ] **7. Malformed-input decode tests**
  Assert `decode` returns `std::nullopt` (not undefined behavior) for:
  a buffer shorter than the expected size for its type byte, an empty
  buffer, and an unrecognized `MessageType` byte value.
  _Satisfies: R2 (robustness implied by "without loss" — decode must
  fail safely, not just succeed correctly)_

- [ ] **8. Zero-heap-allocation verification (NFR1)**
  Instrument a test-scoped global `operator new`/`operator delete`
  counter (same approach as Phase 3's memory-pool verification) around
  calls to `encode` and `decode` for every message type. Assert the
  count is unchanged across each call.
  _Satisfies: NFR1, Definition of Done ("verified by an instrumented
  allocation count")_
  _LEARNING.md: note why this had to be verified with an instrumented
  counter rather than just asserted from the code's structure — a
  refactor later that quietly introduces an allocation (e.g. someone
  changes `std::span` to a `std::vector` parameter) would otherwise go
  unnoticed until the benchmark numbers looked wrong._

- [ ] **9. JSON codec (`adapters/binary_protocol/JsonCodec.hpp/.cpp`)**
  Implement `to_json`/`from_json` (ADL, nlohmann/json convention) for
  all six message types per design.md §4. Round-trip tests mirroring
  task 6 (this codec is allowed to allocate — the test only asserts
  round-trip correctness, not NFR1).
  _Satisfies: R3, R2 (JSON side)_

- [ ] **10. `BinaryProtocolHandler` implementing Phase 5's protocol-handler interface**
  Implement `parse_message`/`serialize_response` against the seam
  Phase 5 already factored out, dispatching to `BinaryCodec`. This is
  the piece that lets the gateway's connection loop stay unaware of
  which protocol it's speaking (design.md §1, §5).
  _Satisfies: R5, design.md §1_

- [ ] **11. Server-startup `--protocol` flag wiring**
  Update `apps/exchange_server/main.cpp` to accept `--protocol=binary`
  (default) or `--protocol=plaintext`, constructing the corresponding
  handler once at startup per design.md §5. No per-connection
  branching.
  _Satisfies: R5, design.md §5_
  _LEARNING.md: why this is a single startup-time choice rather than
  per-connection negotiation — see design.md §5's rejected alternative,
  restate the reasoning in your own words for the record._

- [ ] **12. Gateway integration tests: both protocol modes**
  Start the gateway in `--protocol=binary` mode, exercise a full
  submit/cancel/trade-notification cycle over a real socket, assert
  correct behavior. Repeat for `--protocol=plaintext` and confirm the
  existing Phase 5 behavior is unaffected by this phase's changes.
  _Satisfies: R5_

- [ ] **13. `tools/protocol_benchmark/` — encode/decode latency**
  Reusing Phase 2's `workload_generator` harness format, measure
  encode and decode latency (min/median/p99 over N iterations) for
  binary and JSON, for each of the six message types.
  _Satisfies: R4, design.md §6_

- [ ] **14. `tools/protocol_benchmark/` — payload size and allocation attribution**
  Measure actual encoded byte size (binary `encode()` return value vs.
  `nlohmann::json::dump().size()`) per message type. Capture JSON's
  allocation count during encode/decode (using the same counter
  approach as task 8, applied to the JSON codec this time — expected
  to be nonzero) so the write-up can attribute latency to allocation
  vs. parsing/formatting CPU time separately, per design.md §6.
  _Satisfies: R4, design.md §6_

- [ ] **15. Results write-up with honest interpretation**
  Record benchmark output in the same results format as prior phases.
  Write the interpretation required by the Definition of Done:
  attribute JSON's latency disadvantage (if any) between allocation
  count and parse/format CPU time as separate, reported lines — not a
  single "binary is Nx faster" headline without explanation.
  _Satisfies: Definition of Done ("recorded and written up with an
  honest interpretation")_

- [ ] **16. `docs/LEARNING.md` sweep**
  Confirm every task above that touched code has its LEARNING.md
  entry (tasks 1, 8, 11 call these out explicitly; sweep the rest for
  anything missed). Add a closing Phase 7 summary tying together: the
  network-byte-order-for-arbitrary-width-types approach (§2), the
  zero-allocation verification methodology (task 8), and the
  benchmark's attribution methodology (task 14) as one narrative about
  what the binary-vs-JSON numbers actually mean and don't mean.
  _Satisfies: steering policy_
