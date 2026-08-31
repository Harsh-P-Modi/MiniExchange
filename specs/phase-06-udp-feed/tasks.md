# Phase 6 — Tasks: UDP Market Data Feed

Status: **COMPLETE** — all 23 tasks implemented and test-verified,
built from the v3-revised, approved design.

## Completion summary

- **All 23 tasks done.** `SymbolId`/enriched-event core changes
  (tasks 1–2), the wire message structs (tasks 3–5), `UdpFeedPublisher`
  with the `qty`-vs-`count` draining logic (tasks 7–14), and
  `UdpFeedBookBuilder` with snapshot anchoring + gap detection (tasks
  15–21) are all implemented and exercised by real end-to-end tests
  through a live `MatchingEngine`.
- **Tests:** 47 tests across 7 suites, split across 5 binaries —
  `udp_feed_message_test` (13), `udp_publisher_test` (15),
  `udp_publisher_e2e_test` (3), `udp_book_builder_test` (12),
  `udp_e2e_dod_test` (4, satisfying both Definition-of-Done items:
  reconstruction correctness and gap-detection/staleness) — all
  passing; full `ctest` run = 100% passing.
- **Files:** `core/Types.hpp` (`SymbolId`), `core/Events.hpp`/`Trade.hpp`
  enrichment; `adapters/udp/{FeedMessage.hpp, TopOfBook.hpp,
  udp_feed_publisher.hpp/.cpp, book_builder.hpp/.cpp}`; `adapters_udp`
  library wired in `CMakeLists.txt` (platform-neutral, no UNIX gate).
- **Wired into production:** `apps/exchange_server/main.cpp`
  instantiates a real `UdpFeedPublisher` in place of `NullEventSink`
  (task 22).
- **`docs/LEARNING.md`:** Phase 6 section written, including the
  two-sequence-number distinction, the `qty`-vs-`count` draining logic
  worked through with concrete partial/full-fill examples, and why the
  gap logger is an injected dependency (task 23's closing sweep).

Each task lists the requirements/design sections it satisfies. Every
task that produces or meaningfully changes code ends with a
LEARNING.md note per the steering policy. Task 23 is a final sweep,
not the sole documentation point.

- [x] **1. Add strong-typed `SymbolId` to `core/Types.hpp`; enrich `OrderAccepted`/`OrderCancelled`/`Trade`**
  Add `SymbolId` as a wrapper struct (design.md §2) matching the
  `OrderId`/`Price`/etc. pattern — not a bare `using` alias — plus a
  `std::hash<SymbolId>` specialization alongside the project's
  existing hash specializations. Add `Price` to `OrderAccepted` and
  `Price` + `Side` to `OrderCancelled` (design.md §1a). Add
  `bool resting_order_removed` to `core::Trade` (design.md §1a) —
  `true` when the trade fully consumed the resting counterparty
  order, `false` when it only partially filled it. Update every
  `engine/` call site that constructs these events/trades to populate
  the new fields — the engine already knows all of these values when
  it emits them.
  _Satisfies: design.md §0 (rows 5–7), §1a, §2_
  _LEARNING.md: why SymbolId is a wrapper struct and not a bare alias;
  and why `Trade` needed `resting_order_removed` specifically — the
  publisher can't tell a full fill from a partial one otherwise, and
  that distinction is exactly what task 10's count-vs-qty logic
  depends on._

- [x] **2. Migrate existing event/trade-sink consumers to the enriched payload**
  Update Phase 1's event-sink and trade tests, `NullEventSink`, and
  Phase 5's TCP gateway (if it serializes these events/trades) to
  compile and pass against the new struct shapes. No behavioral
  changes expected — field-count/initializer updates only — but each
  consumer must be checked.
  _Satisfies: design.md §1a ripple effects_
  _LEARNING.md: which consumers needed touching; confirm none
  required behavioral changes (or note any that did and why)._

- [x] **3. Define wire message structs (`adapters/udp/FeedMessage.hpp`)**
  Implement `FeedHeader`, `TopOfBookMessage`, `TradeMessage`,
  `SnapshotMessage` per design.md §2, using `SymbolId`. Document the
  `bid_price = 0` / `ask_price = 0` sentinel meaning "no known best on
  this side" (design.md §1b) directly in the header comments. Add
  `static_assert(std::is_trivially_copyable_v<T>)` for every message
  type.
  _Satisfies: design.md §2_

- [x] **4. Define `TopOfBook` (`adapters/udp/TopOfBook.hpp`)**
  Plain struct per design.md §7 — `{bid_price, bid_qty, ask_price,
  ask_qty}`, with the same zero-sentinel convention as task 3.
  _Satisfies: design.md §7_

- [x] **5. Struct layout and `SymbolId` hash unit tests**
  Assert `sizeof()` of each message type is stable and matches
  hand-computed expectations. Assert
  `std::is_trivially_copyable_v` holds for all message types. Assert
  `SymbolId` works correctly as an `unordered_map` key (construct a
  map, insert/look up by value).
  _Satisfies: design.md §2, §8_

- [x] **6. `adapters/udp` CMake library target**
  Add an `adapters_udp` library target to the top-level
  `CMakeLists.txt`, with correct include paths and a link against
  `core`. Wire it into the test binary that will exercise tasks 5, 9,
  14, 19–21.
  _Satisfies: build system integration_

- [x] **7. `UdpFeedPublisher` skeleton with best-price `qty`/`count` tracking**
  Class shell with `on_trade`, `on_order_accepted`,
  `on_order_cancelled` overrides, constructor taking a fixed
  `SymbolId`, a subscriber list, and a UDP socket. Internal state is
  `{best_bid_price, best_bid_qty, best_bid_count, best_ask_price,
  best_ask_qty, best_ask_count}` per design.md §1b, with `qty` and
  `count` kept as two clearly distinct fields (`qty` = published
  aggregate size, `count` = internal-only order count that decides
  when a side is fully drained). No sending logic yet.
  _Satisfies: R1, design.md §1b_
  _LEARNING.md: why `qty` and `count` are two separate fields rather
  than one — `qty` reaching zero and `count` reaching zero are not the
  same event, and conflating them was exactly the bug the previous
  design draft had._

- [x] **8. Non-blocking send path**
  Implement `sendto()` with `MSG_DONTWAIT`, looping over the
  subscriber list. Handle `EWOULDBLOCK` per design.md §4 (drop and
  continue). Unit test with an injected send function.
  _Satisfies: NFR1, design.md §4_
  _LEARNING.md: contrast this drop policy with Phase 5's TCP
  back-pressure policy._

- [x] **9. Feed-level sequence counter and `timestamp_ns`**
  Add the monotonic `FeedHeader::sequence` counter, incremented once
  per message sent. Add `timestamp_ns` from `CLOCK_MONOTONIC` at send
  time. Wire both into every message type.
  _Satisfies: R2, design.md §2, §3_
  _LEARNING.md: the two-sequence-number distinction and why
  `timestamp_ns` is diagnostic-only._

- [x] **10. `on_trade` → `TradeMessage` + top-of-book update, including drain**
  Build and send a `TradeMessage` carrying `Trade`'s own
  `TradeSequence`. Apply the two independent updates from design.md
  §1b: `qty -= trade.quantity` unconditionally; `count -= 1` **only
  if** `trade.resting_order_removed == true`. If the resulting `count`
  is zero, publish a `TopOfBookMessage` with that side zeroed
  (design.md §1b) rather than guessing the next level — regardless of
  what `qty` computed to. Otherwise publish the updated `qty` as the
  new best size.
  _Satisfies: R1, design.md §1a, §1b, §3_
  _LEARNING.md: walk through both trade cases with concrete examples
  — (1) a partial fill (`resting_order_removed = false`): `qty`
  drops, `count` doesn't, side stays live with a smaller size; (2) a
  full fill of the last order at best (`resting_order_removed =
  true`): `count` hits zero, side gets zeroed regardless of what `qty`
  arithmetic alone would have produced._

- [x] **11. `on_order_accepted` → top-of-book + count update**
  Using the enriched `Price` field: if the new order's price is
  *better* than the current best on its side, replace best price and
  set `count = 1`, publish updated `TopOfBookMessage`. If it's *equal*
  to the current best, increment `count` and publish updated qty. If
  worse, no top-of-book message (doesn't affect top-of-book).
  _Satisfies: R1, design.md §1a, §1b_

- [x] **12. `on_order_cancelled` → top-of-book + count update, including drain**
  Using the enriched `Price`/`Side` fields: if the cancelled order was
  at the current best, `qty -= remaining_qty` and `count -= 1`
  unconditionally — unlike the trade case (task 10), a cancel always
  removes the entire resting order (Phase 1 has no partial-cancel
  operation), so there's no `resting_order_removed`-style ambiguity
  here. If `count` reaches zero, publish a zeroed `TopOfBookMessage`
  for that side. If the cancelled order wasn't at the current best, no
  top-of-book message.
  _Satisfies: R1, design.md §1a, §1b_
  _LEARNING.md: confirm this task and task 10 both key the
  zero-the-side decision off `count`, not `qty` — and note why cancel
  never needs the "was it fully removed" question that trades do._

- [x] **13. Message-count-triggered snapshot emission**
  Counter-based cadence from design.md §5: increment on every message
  sent, emit a `SnapshotMessage` reflecting current best-price/qty
  state (including a zeroed/drained side if that's the live state) at
  threshold `N`, then reset the counter.
  _Satisfies: design.md §5_

- [x] **14. Publisher end-to-end test against a real `MatchingEngine`**
  Attach `UdpFeedPublisher` as a real `EventSink`, drive a scripted
  order sequence that includes at least: a partial fill at the best
  price that does *not* drain it, a full fill that *does* drain it,
  and a cancel that drains the other side. Capture sent bytes, assert
  the wire message sequence — including which messages carry a live
  `qty` vs. a zeroed side — matches expectations for each case.
  _Satisfies: R1, R2, design.md §1b_

- [x] **15. `UdpFeedBookBuilder` skeleton with injectable `GapLogger`**
  Class shell with `on_message` dispatch, `PerSymbolState` (design.md
  §7), and a constructor-injected `GapLogger` (defaulting to a
  stderr-printing implementation). Confirm this file includes nothing
  under `engine/`.
  _Satisfies: R3, design.md §7_
  _LEARNING.md: why the gap logger is an injected dependency rather
  than a hardcoded `fprintf` — same port/adapter pattern as
  `EventSink` itself, and it's what makes task 19's logger-invocation
  assertions possible without capturing stderr._

- [x] **16. Snapshot anchoring logic**
  Implement `apply_snapshot`: sets `anchored = true`, initializes
  `book` and `last_sequence` from `as_of_sequence`. Incrementals with
  `sequence <= as_of_sequence` are discarded as pre-anchor noise.
  _Satisfies: R3, design.md §5_

- [x] **17. Incremental application logic**
  Implement `apply_top_of_book` and `apply_trade` for the anchored
  case, including correctly storing a zeroed/drained side as a valid
  (not missing) state rather than treating `bid_price == 0` as "no
  update happened."
  _Satisfies: R3, design.md §1b_

- [x] **18. Gap detection, staleness flag, and logger invocation**
  Implement `check_sequence`: on `incoming_sequence != last_sequence +
  1` for an anchored symbol, set `stale = true` and call
  `gap_logger_(symbol, expected, received)`. Confirm staleness clears
  only on the next `SnapshotMessage`.
  _Satisfies: R4, design.md §7_

- [x] **19. Book-builder unit tests**
  Scripted wire message sequences — including out-of-order arrival,
  induced gaps, and drained-side messages — fed directly into
  `on_message`. Assert reconstructed state, staleness flag, and
  `GapLogger` invocation arguments (expected vs. received sequence)
  at each step.
  _Satisfies: R3, R4_

- [x] **20. End-to-end reconstruction test (Definition of Done #1)**
  Full pipeline: `MatchingEngine` + `UdpFeedPublisher` → captured wire
  traffic → `UdpFeedBookBuilder`. Assert reconstructed top-of-book
  equals the engine's actual `OrderBook` state, queried directly by
  the test only, after a scripted sequence including at least one
  partial-fill (non-draining) and one full-fill-or-cancel
  (draining) cycle.
  _Satisfies: R3, Definition of Done #1, design.md §1b_

- [x] **21. End-to-end gap-detection test (Definition of Done #2)**
  Same pipeline as task 20, with a chosen subset of packets dropped
  before delivery. Assert `is_stale()` becomes true after the drop and
  clears only once the next `SnapshotMessage` is delivered.
  _Satisfies: R4, Definition of Done #2_

- [x] **22. Wire `UdpFeedPublisher` into `exchange_server`**
  Update `apps/exchange_server/main.cpp` to instantiate a real
  `UdpFeedPublisher` (configured with the engine's `SymbolId` and a
  subscriber list) in place of `NullEventSink`.
  _Satisfies: production composition root_
  _LEARNING.md: the difference between this composition-root wiring
  and the test harness's wiring in task 14._

- [x] **23. `docs/LEARNING.md` sweep**
  Confirm every task above that touched code has its LEARNING.md
  entry; sweep for anything missed. Add a closing Phase 6 summary
  tying together: core-event/trade enrichment (tasks 1–2), the
  two-sequence-number distinction, drop-on-backpressure, message-count
  snapshots, and the `qty`-vs-`count` best-price draining logic (§1b)
  as a single coherent narrative about what a top-of-book-only feed
  can and can't promise.
  _Satisfies: steering policy_
