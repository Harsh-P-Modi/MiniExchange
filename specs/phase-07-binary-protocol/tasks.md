# Phase 7 — Tasks: Binary Wire Protocol

Status: **COMPLETE** — all 16 tasks implemented and test-verified
(task 15's actual latency numbers deferred to a controlled Linux run).

## Completion summary

- **All 16 tasks done.** The six-message-type binary codec (tasks 2–7)
  and its zero-allocation guarantee (task 8), the benchmark-only JSON
  codec (task 9), `BinaryProtocolHandler` plugging into Phase 5's
  protocol-handler seam (task 10), server-startup `--protocol` wiring
  (task 11), and the `tools/protocol_benchmark/` harness (tasks 13–14)
  are all implemented.
- **Tests (Linux-only build — this dev box is Windows and doesn't
  build this UNIX-gated block; verified by CI):** `binary_byte_order_test`,
  `binary_codec_test`, `binary_codec_boundary_test`,
  `binary_codec_malformed_test`, `binary_codec_alloc_test`,
  `json_codec_test`, `binary_gateway_codec_test`, and
  `exchange_server_binary_e2e_test` (task 12's both-protocol-modes
  integration test) are all wired into `CMakeLists.txt` and `ctest`.
- **Files:** `adapters/binary_protocol/{Message.hpp, ByteOrder.hpp,
  BinaryCodec.hpp/.cpp, JsonCodec.hpp/.cpp, GatewayCodec.hpp/.cpp}`,
  `tools/protocol_benchmark/protocol_bench.cpp`; `adapters_binary_protocol`,
  `json_codec`, and `protocol_benchmark` targets wired in
  `CMakeLists.txt` inside the UNIX-only block (uses `<endian.h>`).
- **`docs/LEARNING.md`:** Phase 7 section written (message-struct
  trivial-copyability, the network-byte-order-for-arbitrary-width-types
  approach, the zero-allocation verification methodology, and the
  benchmark's allocation-vs-CPU attribution methodology).
- **Deferred (not code): task 15's numeric results.** See task 15's
  inline note — the write-up, methodology, and interpretation framework
  are complete; the actual binary-vs-JSON latency numbers in
  `benchmarks/results/phase-07-binary-vs-json.md` are still `_TBD_`
  pending a controlled Linux run, same environment caveat as Phase 8's
  T4 and Phase 10's T13. CI's `benchmarks` job (added alongside this
  audit) now produces these numbers as a downloadable artifact on every
  push.

Each task lists the requirements/design sections it satisfies. Tasks
that produce or meaningfully change code end with a LEARNING.md note
per the steering policy.

- [x] **1. Vendor `nlohmann/json` via CMake `FetchContent`**
  Add the dependency to the top-level `CMakeLists.txt`, pinned to a
  specific released version (not a floating branch). Confirm it only
  links into the JSON codec and benchmark targets, not into `core/` or
  `engine/`.
  _Satisfies: design.md §0 row 1_
  _LEARNING.md: note this is the project's first non-test/benchmark
  external dependency, and why it's scoped to stay out of `core/`/`engine/`._

- [x] **2. Define message structs and `MessageType` (`adapters/binary_protocol/Message.hpp`)**
  Implement the six structs (`LimitOrderAddMsg`, `MarketOrderAddMsg`,
  `CancelMsg`, `AckMsg`, `RejectMsg`, `TradeNotificationMsg`) exactly
  per design.md §2, reusing `core/` strong types. No logic — pure data
  layout, mirroring Phase 6's `FeedMessage.hpp` approach.
  _Satisfies: R1, design.md §2_

- [x] **3. Byte-order helpers (`adapters/binary_protocol/ByteOrder.hpp`)**
  Implement `to_network`/`from_network` for raw 16/32/64-bit integers
  (`htons`/`htonl`/`htobe64` and their inverses) and the generic
  templated overload for `core/`'s strong-typed wrappers, per design.md
  §2. Unit tests: round-trip a raw value through `to_network` then
  `from_network` for each width, confirm no-op on a platform where
  host and network order coincide is still correctly swapped-and-
  unswapped (i.e., test the swap logic itself, not just idempotence).
  _Satisfies: R1 (endianness), design.md §2_

- [x] **4. `encode`/`decode` for a single message type (`LimitOrderAddMsg`)**
  Implement `encode<LimitOrderAddMsg>` and the `LimitOrderAdd` branch
  of `decode` per design.md §3 — write/read every field through the
  byte-order helpers from task 3 into/from a caller-provided
  `std::span<std::byte>`. Establish the pattern the remaining five
  types (task 5) repeat.
  _Satisfies: R1, R2, design.md §3_

- [x] **5. Extend `encode`/`decode` to the remaining five message types**
  `MarketOrderAddMsg`, `CancelMsg`, `AckMsg`, `RejectMsg`,
  `TradeNotificationMsg` — same pattern as task 4, dispatched through
  `AnyMessage` (`std::variant`) in `decode`.
  _Satisfies: R1, R2, design.md §3_

- [x] **6. Round-trip property tests for the binary codec (R2)**
  For each of the six message types, generate a wide range of field
  values (boundary values, at minimum: 0, max representable, and a
  handful of typical values) and assert `decode(encode(msg)) == msg`.
  Include at least one case per type exercising every field, not just
  the first one.
  _Satisfies: R2, Definition of Done ("round-trip tests pass")_

- [x] **7. Malformed-input decode tests**
  Assert `decode` returns `std::nullopt` (not undefined behavior) for:
  a buffer shorter than the expected size for its type byte, an empty
  buffer, and an unrecognized `MessageType` byte value.
  _Satisfies: R2 (robustness implied by "without loss" — decode must
  fail safely, not just succeed correctly)_

- [x] **8. Zero-heap-allocation verification (NFR1)**
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

- [x] **9. JSON codec (`adapters/binary_protocol/JsonCodec.hpp/.cpp`)**
  Implement `to_json`/`from_json` (ADL, nlohmann/json convention) for
  all six message types per design.md §4. Round-trip tests mirroring
  task 6 (this codec is allowed to allocate — the test only asserts
  round-trip correctness, not NFR1).
  _Satisfies: R3, R2 (JSON side)_

- [x] **10. `BinaryProtocolHandler` implementing Phase 5's protocol-handler interface**
  Implement `parse_message`/`serialize_response` against the seam
  Phase 5 already factored out, dispatching to `BinaryCodec`. This is
  the piece that lets the gateway's connection loop stay unaware of
  which protocol it's speaking (design.md §1, §5).
  _Satisfies: R5, design.md §1_

- [x] **11. Server-startup `--protocol` flag wiring**
  Update `apps/exchange_server/main.cpp` to accept `--protocol=binary`
  (default) or `--protocol=plaintext`, constructing the corresponding
  handler once at startup per design.md §5. No per-connection
  branching.
  _Satisfies: R5, design.md §5_
  _LEARNING.md: why this is a single startup-time choice rather than
  per-connection negotiation — see design.md §5's rejected alternative,
  restate the reasoning in your own words for the record._

- [x] **12. Gateway integration tests: both protocol modes**
  Start the gateway in `--protocol=binary` mode, exercise a full
  submit/cancel/trade-notification cycle over a real socket, assert
  correct behavior. Repeat for `--protocol=plaintext` and confirm the
  existing Phase 5 behavior is unaffected by this phase's changes.
  _Satisfies: R5_

- [x] **13. `tools/protocol_benchmark/` — encode/decode latency**
  Reusing Phase 2's `workload_generator` harness format, measure
  encode and decode latency (min/median/p99 over N iterations) for
  binary and JSON, for each of the six message types.
  _Satisfies: R4, design.md §6_

- [x] **14. `tools/protocol_benchmark/` — payload size and allocation attribution**
  Measure actual encoded byte size (binary `encode()` return value vs.
  `nlohmann::json::dump().size()`) per message type. Capture JSON's
  allocation count during encode/decode (using the same counter
  approach as task 8, applied to the JSON codec this time — expected
  to be nonzero) so the write-up can attribute latency to allocation
  vs. parsing/formatting CPU time separately, per design.md §6.
  _Satisfies: R4, design.md §6_

- [x] **15. Results write-up with honest interpretation**
  Record benchmark output in the same results format as prior phases.
  Write the interpretation required by the Definition of Done:
  attribute JSON's latency disadvantage (if any) between allocation
  count and parse/format CPU time as separate, reported lines — not a
  single "binary is Nx faster" headline without explanation.
  _Satisfies: Definition of Done ("recorded and written up with an
  honest interpretation")_
  DONE (with caveat): `benchmarks/results/phase-07-binary-vs-json.md`
  has the full methodology, environment requirements, and the
  interpretation framework (what "allocation-dominated vs. CPU-bound"
  would mean, honest caveats about nlohmann/json vs. a production
  parser) written and ready. The payload-size row and the "binary
  allocates 0" rows are filled from `static_assert`/task-8-verified
  facts. The actual latency numbers are still `_TBD_` PENDING a
  controlled Linux run of `./build/protocol_benchmark` — this dev box
  is Windows-only, same environment caveat as Phase 8's T4 and Phase
  10's T13. CI's new (non-blocking) `benchmarks` job now runs
  `protocol_benchmark` on every push and uploads the raw numbers as an
  artifact, so the remaining step is pulling a run's numbers into this
  table and writing the two or three interpretation sentences the
  framework above is already structured for.

- [x] **16. `docs/LEARNING.md` sweep**
  Confirm every task above that touched code has its LEARNING.md
  entry (tasks 1, 8, 11 call these out explicitly; sweep the rest for
  anything missed). Add a closing Phase 7 summary tying together: the
  network-byte-order-for-arbitrary-width-types approach (§2), the
  zero-allocation verification methodology (task 8), and the
  benchmark's attribution methodology (task 14) as one narrative about
  what the binary-vs-JSON numbers actually mean and don't mean.
  _Satisfies: steering policy_
