# Phase 6 — Requirements: UDP Market Data Feed

Status: **APPROVED — open questions resolved, see design.md §0**

## 1. Scope

`adapters/udp/` — a publisher implementing `EventSink` that broadcasts
every `Trade`/`OrderAccepted`/`OrderCancelled` event over UDP (real
multicast if the dev environment supports it, simulated via unicast
fan-out otherwise), plus a client-side book-builder that reconstructs
a local order book replica purely from the feed — proving the feed
carries enough information to be useful, not just "sends packets."

This is the phase that makes the `EventSink` port (introduced all the
way back in Phase 1, unused until now) actually earn its place in the
architecture — the whole reason it exists as a *separate* channel from
`EngineResponse` is so this adapter can subscribe without touching
`engine/` at all.

## 2. Functional Requirements (EARS)

- R1: THE PUBLISHER SHALL send a message immediately upon
  `EventSink::on_trade` being called by the engine, containing at
  minimum the full `Trade` (§ Phase 1 `core/Trade.hpp`).
- R2: THE PUBLISHER SHALL include a monotonic feed-level sequence
  number (`FeedHeader::sequence`) in every message it sends — trade
  messages, top-of-book messages, and snapshots alike — allowing
  subscribers to detect gaps (UDP is unreliable/unordered — this is
  the mechanism a subscriber uses to notice a dropped packet, which is
  a real market-data concern worth demonstrating understanding of).
  **Correction (post-design.md review): earlier drafts of this
  requirement pointed at `core::Trade`'s own `TradeSequence` for gap
  detection. That field is engine-level trade ordering, not a
  transport-level counter, and doesn't exist on non-trade messages —
  see design.md §3 for why a separate feed-level sequence is required
  instead.**
- R3: THE BOOK-BUILDER (subscriber/client) SHALL reconstruct a local
  view of the book from the feed alone, without any direct access to
  the engine's `OrderBook` — this is the acceptance test that proves
  the feed is sufficient.
- R4: WHEN the book-builder detects a gap in `FeedHeader::sequence`,
  IT SHALL flag its local state as potentially stale rather than
  silently continuing (real feeds solve this with snapshot+recovery; a
  Phase 1 version of this can simply log/flag the gap — see design.md
  §5 for how staleness is cleared).
  **Correction (post-design.md review): as with R2, gap detection is
  on the feed-level sequence, not `TradeSequence` — a gap in a
  top-of-book message (which carries no `TradeSequence` at all) must
  still be detectable.**

## 3. Non-Functional Requirements

- NFR1: Publishing SHALL NOT block or meaningfully slow down the
  matching engine's hot path — `on_trade` being called synchronously
  (Phase 1 R20) means the publish call itself needs to be fast
  (non-blocking socket send) or handed off, not a source of new
  latency in the matching loop.

## 4. Definition of Done

- Book-builder's reconstructed book matches the engine's actual book
  state after a test sequence of trades (verified by comparing against
  the engine's own `OrderBook` in a test, even though the book-builder
  itself doesn't have access to it in the "real" scenario).
- Gap detection demonstrated under artificial packet loss in a test.

## 5. Open Questions — RESOLVED (see design.md §0 for binding resolutions)

1. **Feed depth** → Top-of-book + trade prints. No full depth-of-book.
2. **Snapshot + incremental** → In scope. Snapshot cadence is every N
   messages published (see design.md §5).
3. **Message format** → Plain fixed-layout structs, `memcpy`'d
   on/off the wire. Baseline for Phase 7 comparison.
4. **Real multicast vs. simulated** → Simulated unicast fan-out to a
   static subscriber list. Multicast documented, not exercised.

## 6. Amendments From Design-Time Review

- **`SymbolId` added to `core/Types.hpp`.** The engine is currently
  single-symbol, but the wire format and `EventSink` consumers
  (including this feed) are meant to be forward-compatible with
  Phase 10+. Each engine instance is configured with one fixed
  `SymbolId` at startup; this is not a multi-symbol engine change.
- **`OrderAccepted` and `OrderCancelled` are enriched with `Price`
  (both) and `Side` (cancel only).** The original event payloads
  (`{id, side, quantity}` / `{id, remaining_qty}`) don't carry enough
  information for any `EventSink` consumer — not just this feed — to
  determine whether an accept/cancel changed top-of-book. This is a
  `core/` schema change with ripple effects into every existing event
  consumer (Phase 1 tests, `NullEventSink`, Phase 5's TCP gateway) —
  see design.md §1a and tasks.md tasks 1–2 for the migration.
