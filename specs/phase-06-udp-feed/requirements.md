# Phase 6 — Requirements: UDP Market Data Feed

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

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
- R2: THE PUBLISHER SHALL include `TradeSequence` in every trade
  message, allowing subscribers to detect gaps (UDP is
  unreliable/unordered — this is the mechanism a subscriber uses to
  notice a dropped packet, which is a real market-data concern worth
  demonstrating understanding of).
- R3: THE BOOK-BUILDER (subscriber/client) SHALL reconstruct a local
  view of the book from the feed alone, without any direct access to
  the engine's `OrderBook` — this is the acceptance test that proves
  the feed is sufficient.
- R4: WHEN the book-builder detects a `TradeSequence` gap, IT SHALL
  flag its local state as potentially stale rather than silently
  continuing (real feeds solve this with snapshot+recovery; a Phase 1
  version of this can simply log/flag the gap — see Open Questions for
  how far to take this).

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

## 5. Open Questions (resolve before design.md for this phase)

1. **Feed depth** — top-of-book only, full depth (every price level),
   or trade-prints only with no book-state reconstruction at all (the
   simplest option, but doesn't fully demonstrate "market data feed"
   as a concept)? Full depth is the most impressive but the most work;
   worth deciding deliberately rather than defaulting to "whatever's
   easiest."
2. **Snapshot + incremental, or pure incremental?** Real feeds
   typically publish periodic full snapshots plus incremental updates
   so a late-joining subscriber can catch up. Is that in scope for
   Phase 6, or acceptable to defer/note as a known limitation?
3. **Message format** — plain struct + `memcpy` (simplest, ties into
   Phase 7's binary-protocol work naturally) vs. something else?
   Leaning toward defining it now in a way Phase 7 can directly reuse
   or compare against, rather than inventing a third format.
4. **Real multicast vs. simulated** — does your dev environment
   (WSL/Linux VM/local network) support testing actual UDP multicast,
   or should this default to unicast fan-out to a known list of
   subscribers, with multicast noted as "how a real exchange would do
   it" in the write-up rather than actually exercised?
