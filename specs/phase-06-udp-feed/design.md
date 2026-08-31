# Phase 6 — Design: UDP Market Data Feed

Status: **APPROVED (v3)** — `tasks.md` is written from this version
and the phase is implemented and test-verified.

Revision note: supersedes the v2 draft. This pass resolves: best-price
draining on cancel/trade (§1b, new), `SymbolId` strong-typing (§2),
and explicit gap-logging mechanism (§7). Everything else carries over
from v2 intact.

## 0. Resolved Open Questions (from requirements.md)

| # | Question | Resolution |
|---|----------|------------|
| 1 | Feed depth | Top-of-book (best bid/ask, size) + trade prints. No full depth-of-book in Phase 6. |
| 2 | Snapshot + incremental | In scope. Snapshot emitted every N messages published, not wall-clock. |
| 3 | Message format | Plain fixed-layout structs, `memcpy`'d directly on/off the wire. Baseline Phase 7 compares its binary protocol against. |
| 4 | Multicast | Simulated via unicast fan-out to a static subscriber list. Real multicast documented, not exercised. |
| 5 | `SymbolId` | Added to `core/Types.hpp` as a strong-typed wrapper (§2), consistent with `OrderId`/`Price`/etc. Each engine instance is configured with one fixed `SymbolId` at startup. |
| 6 | `OrderAccepted`/`OrderCancelled` payload | Enriched with `Price` (both) and `Side` (cancel only) — see §1a. |
| 7 (added in v3) | Best-price draining | Publisher tracks `qty` (published aggregate size) and `count` (internal-only order count) separately at its best bid/ask; `count` reaching zero — not `qty` — is what triggers publishing an explicit empty side. Requires `Trade.resting_order_removed` (§1a) to resolve unambiguously. See §1b. |

These resolutions are binding for this design; if any of them need to change, that's a requirements.md amendment, not a design-time judgment call.

## 1. Architecture Overview

```
                    ┌─────────────────────┐
   engine/          │   MatchingEngine     │
   (Phase 1)        │   calls EventSink    │
                    └──────────┬───────────┘
                               │ on_trade / on_order_accepted / on_order_cancelled
                               ▼
                    ┌─────────────────────┐
   adapters/udp/    │  UdpFeedPublisher   │  (implements EventSink)
                    │  - best bid/ask +    │
                    │    order count at    │
                    │    each (§1b)        │
                    │  - sequence counter  │
                    │  - message-count     │
                    │    snapshot trigger  │
                    └──────────┬───────────┘
                               │ non-blocking sendto() x N subscribers
                               ▼
                    ┌─────────────────────┐
                    │   UDP socket(s)      │  simulated fan-out
                    └──────────┬───────────┘
                               │ (lossy, unordered — simulated in tests)
                               ▼
                    ┌─────────────────────┐
   adapters/udp/    │   BookBuilder        │  (subscriber-side, no engine access)
                    │  - local TopOfBook   │
                    │  - gap detector +    │
                    │    injectable logger │
                    │  - staleness flag    │
                    └─────────────────────┘
```

Two independent components live under `adapters/udp/`:

- **Publisher** (`UdpFeedPublisher`): wired in as an `EventSink`. It never talks to `OrderBook` directly — it derives top-of-book state purely from the enriched event payloads it's handed (§1a), the same information a subscriber gets over the wire. This keeps the "book-builder proves the feed is sufficient" acceptance test honest.
- **BookBuilder** (`UdpFeedBookBuilder`): sits entirely on the subscriber side. No reference to `engine/` is reachable from this class (enforced by header inclusion, see §9 Testing).

## 1a. Core Event/Trade Enrichment

```cpp
// core/Events.hpp — BEFORE
struct OrderAccepted  { OrderId id; Side side; Quantity quantity; };
struct OrderCancelled { OrderId id; Quantity remaining_qty; };

// core/Events.hpp — AFTER
struct OrderAccepted  { OrderId id; Side side; Quantity quantity; Price price; };
struct OrderCancelled { OrderId id; Quantity remaining_qty; Side side; Price price; };

// core/Trade.hpp — BEFORE (fields other than TradeSequence omitted; unchanged)
struct Trade { /* ... */ TradeSequence trade_sequence; };

// core/Trade.hpp — AFTER
struct Trade { /* ... */ TradeSequence trade_sequence; bool resting_order_removed; };
```

`OrderCancelled` gains `Side` and `Price`; `OrderAccepted` gains `Price`. `Trade` gains `resting_order_removed`: `true` if this trade fully consumed the resting (passive) counterparty order, so it is no longer in the book afterward; `false` if the resting order was only partially filled and is still resting. The engine already knows this at trade-generation time — it's the same fact that determines whether the engine removes the resting order from its internal book or leaves it there with reduced quantity — so this is plumbing, not new logic. (This assumes, consistent with the Phase 1 matching engine, that each `Trade` corresponds to exactly one aggressor/resting pair — an aggressive order sweeping multiple levels produces multiple `Trade` events, not one aggregated one.)

This is a `core/` schema change with a checkable, bounded ripple: every `engine/` construction site of these events/trades (Phase 1) is updated to populate the new fields, and every existing consumer — Phase 1's event-sink and trade tests, `NullEventSink`, Phase 5's TCP gateway — is recompiled and verified against the new shape (tasks.md tasks 1–2, sequenced before any UDP code). `resting_order_removed` is engine-internal state exposed on `Trade`; it is **not** added to the wire `TradeMessage` (§2) — subscribers only need price/quantity for trade prints, and the drain-related consequence of this flag is already fully captured in whatever `TopOfBookMessage` the publisher emits as a result (§1b).

## 1b. Best-Price Draining: `qty` vs. `count`

The publisher's internal state is intentionally shallow — top-of-book only (§2), not full depth. If a cancel or trade removes the *last* order resting at the current best price, the publisher has no way to know the new best price — that lives in levels behind the one it tracks, which it deliberately doesn't maintain. Handling this requires the publisher to track **two distinct things per side**, and earlier drafts of this design conflated them:

- **`qty`** — the aggregate resting quantity at the tracked best price. This is exactly what gets published as `bid_qty`/`ask_qty` (§2). It changes by however much quantity a given event actually adds or removes, regardless of whether that event fully or only partially affects any single order.
- **`count`** — the number of *distinct orders* currently resting at the tracked best price. This is internal bookkeeping only, **never published on the wire** — its sole purpose is answering "has this price level gone completely empty?" so the publisher knows when to zero the side rather than continue publishing a stale price with a (possibly wrong) qty.

A price level drains — and the publisher zeroes that side — exactly when `count` reaches zero. `qty` reaching zero is a *consequence* of that in the normal case, not the trigger, because `qty` alone can't distinguish "the one remaining order emptied out" from "one of several orders emptied out, others still resting" without also knowing how many orders there are.

Per-event update rules:

- **`on_order_accepted`**: a single order arriving is unambiguous. At a price *better* than the current best: replace — new price, `qty = order.quantity`, `count = 1`. At a price *equal* to the current best: `qty += order.quantity`, `count += 1`. At a worse price: no tracked change.
- **`on_order_cancelled`**: the Phase 1 engine has no partial-cancel/reduce operation — a cancel always removes the entire resting order named by `id`. At the current best price: `qty -= remaining_qty`, `count -= 1`, unconditionally. (If `price`/`side` don't match the currently tracked best, no tracked change — the cancel happened at a level the publisher isn't tracking.)
- **`on_trade`**: this is the case that was previously ambiguous. `qty -= trade.quantity` always, regardless of `resting_order_removed`. `count -= 1` **only when `trade.resting_order_removed == true`**; if `false`, the resting order is still there with less quantity, so the order count at that price is unchanged. Without the `resting_order_removed` flag (§1a), the publisher would have no way to tell these two cases apart from `Trade` alone — that's precisely the gap this enrichment closes.
- **When `count` reaches zero** for a side, the publisher publishes a `TopOfBookMessage` with that side explicitly zeroed (`bid_price = 0, bid_qty = 0`, or the ask equivalent) as a sentinel for "no known best on this side," rather than guessing at the next level. It does not attempt to reconstruct what's behind the drained level.
- The subscriber's view is corrected at the next message-count-triggered snapshot (§5), which always reflects the publisher's live `qty`/`count` state, including any level the engine has meanwhile re-populated.

This is an accepted, documented limitation of top-of-book-only feeds, not a defect — real incremental feeds exhibit the same brief "no best" gap between a level draining and the next snapshot or a new order arriving at a new best. Full depth tracking would eliminate the gap but was explicitly ruled out of scope (§0 row 1, §6 "why not full depth-of-book").

**Rejected alternative**: adding a `std::optional<Price> new_best_after` field so the engine tells the publisher what's behind a drained level. Rejected because it's book-structure knowledge — a materially larger and different kind of leakage than `resting_order_removed`, which describes only the trade's own two participants, not the rest of the book.

## 2. Wire Format

All messages are fixed-size, POD (plain-old-data), no padding surprises — `static_assert(std::is_trivially_copyable_v<T>)` on every message type.

```cpp
// core/Types.hpp — addition
struct SymbolId {
    uint32_t value;
    friend bool operator==(SymbolId, SymbolId) noexcept = default;
};
// Consistent with OrderId/Price/etc.'s strong-typed wrapper pattern —
// a bare `using SymbolId = uint32_t` would let a SymbolId be silently
// mixed with any other uint32_t, which the project has avoided since
// Phase 1 for every other domain primitive.

// core/Hash.hpp (wherever OrderId's/ClientId's specializations live) — addition
template<>
struct std::hash<SymbolId> {
    size_t operator()(SymbolId id) const noexcept {
        return std::hash<uint32_t>{}(id.value);
    }
};
// Required because BookBuilder keys an unordered_map on SymbolId (§7).

// adapters/udp/FeedMessage.hpp

enum class MessageType : uint8_t {
    TopOfBook  = 1,
    Trade      = 2,
    Snapshot   = 3,
};

struct FeedHeader {
    MessageType type;
    uint8_t     _pad[3];   // explicit, not compiler-inserted — keeps layout obvious
    uint64_t    sequence;      // monotonic per-publisher feed-level sequence (§3) — used for gap detection (R2/R4)
    uint64_t    timestamp_ns;  // CLOCK_MONOTONIC, publish-time — for publish→receive latency
                                // diagnostics only; NOT wall-clock, NOT a business timestamp.
};

struct TopOfBookMessage {
    FeedHeader header;
    SymbolId   symbol;
    Price      bid_price;   // 0 = no known best on this side (see §1b draining)
    Quantity   bid_qty;
    Price      ask_price;   // 0 = no known best on this side
    Quantity   ask_qty;
};

struct TradeMessage {
    FeedHeader header;
    SymbolId   symbol;
    Price      price;
    Quantity   quantity;
    TradeSequence trade_sequence;   // Phase 1 core::Trade's own sequence — engine-level
                                     // ordering, distinct from header.sequence. See §3.
};

struct SnapshotMessage {
    FeedHeader header;
    SymbolId   symbol;
    Price      bid_price;
    Quantity   bid_qty;
    Price      ask_price;
    Quantity   ask_qty;
    uint64_t   as_of_sequence;      // "this snapshot reflects feed state as of sequence N"
};
```

Each `UdpFeedPublisher` instance is constructed with one fixed `SymbolId`, set from process/engine startup configuration. Nothing in Phase 6 makes the engine multi-symbol — only the wire format is shaped to not require a breaking change when Phase 10 does.

## 3. Two Sequence Numbers, Not One

`TradeSequence` (Phase 1's `core::Trade`) and `FeedHeader::sequence` are **not the same counter**. `TradeSequence` is engine-level trade ordering, independent of any feed. `FeedHeader::sequence` is a feed-transport-level counter incremented once per message *sent on the wire* (top-of-book and snapshot messages included). Gap detection (R2/R4) uses `FeedHeader::sequence` specifically because it has to catch gaps in messages — like a drained top-of-book update (§1b) — that carry no `TradeSequence` at all.

## 4. Publisher: Non-Blocking Hot Path (NFR1)

1. On each `EventSink` callback, the publisher updates its in-memory best-price/count state (§1b) — cheap, a handful of field writes — and builds the wire message in a stack-allocated buffer.
2. `sendto()` is issued with `MSG_DONTWAIT` on a socket with a bounded OS send buffer.
3. On `EWOULDBLOCK`, the message for that specific subscriber is **dropped, not retried, not queued** — consistent with UDP's own semantics; gap detection (R4) is what makes dropping acceptable.
4. Fan-out to N simulated subscribers is N independent non-blocking `sendto()` calls, not multicast (§6).

No locks, no allocation, no blocking syscalls on the hot path.

## 5. Snapshot + Incremental

- Snapshot cadence is triggered by **message count**, not wall-clock: an internal counter increments on every message sent; at threshold `N` (default e.g. 500) the publisher emits a `SnapshotMessage` reflecting its *current* best-price/qty state — including a drained (§1b) side if that's the live state at snapshot time — and resets the counter.
- Snapshots and incrementals share the same `sequence` counter space (§3).
- A late-joining `BookBuilder` starts **uninitialized** (distinct from "stale") and only applies incrementals once anchored by a `SnapshotMessage`. Incrementals with `sequence <= as_of_sequence` are discarded as pre-anchor noise, not gaps.

## 6. Why Not X

- **Why not real multicast?** Not guaranteed under WSL/VM networking; simulated unicast fan-out preserves every behavior this phase needs to demonstrate.
- **Why not full depth-of-book?** Would eliminate the §1b draining gap, but at the cost of tracking and diffing every price level — out of proportion to what this phase needs to demonstrate (sequencing, gap detection, snapshot/incremental reconciliation already fully exercise the interesting parts).
- **Why not TCP for the feed?** Would make gap detection untestable by construction; the reliability/latency trade-off is the point being demonstrated.
- **Why not protobuf/FlatBuffers?** Would obscure the byte layout this phase is meant to make explicit, and front-load Phase 7's format decisions.
- **Why not a queue + dedicated publisher thread?** Non-blocking `sendto()` is already fast enough; revisit only if benchmarking shows measured hot-path impact.
- **Why hardcode SymbolId instead of a dynamic registry?** The engine is single-symbol; a registry is speculative infrastructure Phase 6 doesn't need.
- **Why enrich core events instead of giving the publisher `OrderBook` access?** Direct access would let the publisher "cheat," undermining the book-builder acceptance test (R3). Enrichment is a small, bounded, checkable change.
- **Why not have the engine tell the publisher what's behind a drained level (§1b)?** Rejected — that's book-structure knowledge, a materially larger and different kind of leakage than the order's own price/side, and it's exactly the shortcut full-depth tracking would represent, just smuggled in through the event payload instead of an explicit scope change.
- **Why track `count` separately from `qty` instead of just checking `qty == 0` (§1b)?** `qty == 0` can't distinguish "the single remaining order at this price just emptied out" from a bookkeeping inconsistency, and more importantly a partial fill already drives `qty` down without the level being empty — `count` is the only signal that actually answers "are there zero orders left here," which is what the zero-the-side decision needs.

## 7. `TopOfBook` Type and BookBuilder Reconstruction

```cpp
// adapters/udp/TopOfBook.hpp
struct TopOfBook {
    Price    bid_price;   // 0 = no known best (§1b)
    Quantity bid_qty;
    Price    ask_price;   // 0 = no known best (§1b)
    Quantity ask_qty;
};
```

```cpp
// adapters/udp/BookBuilder.hpp — no #include of anything under engine/

using GapLogger = std::function<void(SymbolId, uint64_t expected_sequence, uint64_t received_sequence)>;

class UdpFeedBookBuilder {
public:
    // gap_logger defaults to a stderr-printing implementation if not supplied;
    // tests inject a capturing lambda instead of parsing stderr output.
    explicit UdpFeedBookBuilder(GapLogger gap_logger = default_stderr_gap_logger());

    void on_message(const std::byte* data, size_t len);   // dispatches on FeedHeader::type

    std::optional<TopOfBook> top_of_book(SymbolId) const;
    bool is_stale(SymbolId) const;

private:
    void apply_snapshot(const SnapshotMessage&);
    void apply_top_of_book(const TopOfBookMessage&);
    void apply_trade(const TradeMessage&);
    void check_sequence(SymbolId, uint64_t incoming_sequence);   // R4 gap detection; calls gap_logger_ on mismatch

    GapLogger gap_logger_;

    struct PerSymbolState {
        std::optional<TopOfBook> book;
        uint64_t last_sequence = 0;
        bool     anchored = false;
        bool     stale = false;
    };
    std::unordered_map<SymbolId, PerSymbolState> symbols_;
};
```

Gap detection: if `incoming_sequence != last_sequence + 1` for an already-anchored symbol, set `stale = true` and invoke `gap_logger_(symbol, last_sequence + 1, incoming_sequence)`. The logger is an explicit constructor-injected dependency — same pattern as the `EventSink` port itself — rather than a hardcoded `fprintf`, so tests assert on logger invocations directly instead of capturing stderr. Staleness clears only on the next `SnapshotMessage`, not on sequence resumption.

## 8. Testing Strategy

- **Unit**: message struct layout (`static_assert` sizes, trivially-copyable); `SymbolId`'s `std::hash` specialization (used correctly as an `unordered_map` key).
- **Core event migration**: Phase 1 event-sink and trade tests, plus `NullEventSink`, recompiled and passing against the enriched payload (including `Trade.resting_order_removed`) before any UDP code is written.
- **Publisher**: scripted `EventSink`/`Trade` sequences → assert emitted wire messages, explicitly covering all three `count`-affecting cases: (a) a partial fill (`resting_order_removed = false`) that reduces `qty` but leaves the side non-zeroed, (b) a fill that fully consumes the last order at best (`resting_order_removed = true`) and triggers zeroing, and (c) a cancel (always a full removal) triggering zeroing. Also assert recovery once a new best is established after a drain.
- **BookBuilder**: scripted wire messages (including snapshots, drained sides, and induced gaps) → assert reconstructed state, staleness flag, and `gap_logger_` invocations with correct expected/received sequence values.
- **End-to-end (Definition of Done #1)**: real `MatchingEngine` + `UdpFeedPublisher` → captured wire traffic → `UdpFeedBookBuilder`; assert reconstructed top-of-book equals the engine's actual `OrderBook`, queried directly by the test only.
- **Gap detection (Definition of Done #2)**: same pipeline with a subset of packets dropped; assert `is_stale()` becomes true and clears only after the next snapshot.

## 9. Non-Goals

- No congestion control, retransmission, or reliability layer.
- No authentication/entitlement on subscribers.
- No multi-symbol load beyond proving the mechanism for one symbol.
- No dynamic symbol registry.
- No full depth-of-book, and therefore no attempt to reconstruct what's behind a drained best level from anything other than the next snapshot (§1b).
