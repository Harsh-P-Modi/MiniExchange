# Phase 5 — Requirements: TCP Order Gateway

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

## 1. Scope

`adapters/tcp/` — a reusable library implementing an epoll-based,
nonblocking TCP server that accepts multiple concurrent client
connections, parses order commands, and feeds them through the Phase 4
queue to the (still single-threaded) matching engine, then routes
`EngineResponse`s back to the originating client.

**⚠️ Cross-cutting concern flagged here (also flagged in Phase 8):**
this is the first phase with genuinely multiple concurrent clients.
Phase 1's `OrderId` uniqueness is global and client-supplied — with
multiple TCP clients, something needs to track *which socket/session
submitted which order* (for routing responses back), and Phase 8's
self-trade-prevention needs a `ClientId` concept that doesn't exist
anywhere yet. **Recommendation:** introduce `ClientId` here in Phase 5
(as part of the session/connection model), not wait until Phase 8 —
Phase 8 then just consumes a concept that already exists rather than
inventing it under a different phase's spec. Flagging rather than
deciding, since it affects this phase's design non-trivially.

## 2. Functional Requirements (EARS)

- R1: THE SERVER SHALL listen on a configurable TCP port and accept
  multiple concurrent client connections using `epoll` in
  edge-triggered, nonblocking mode.
- R2: THE SERVER SHALL set `TCP_NODELAY` on every accepted client
  socket (disable Nagle's algorithm) — latency over throughput,
  consistent with the project's whole premise.
- R3: THE SERVER SHALL run its I/O loop on a thread separate from the
  matching engine's thread, communicating exclusively through the
  Phase 4 queue in the engine-ward direction.
- R4: Since TCP is a byte stream, not a message stream, THE SERVER
  SHALL implement explicit message framing (see Open Questions for
  which) and buffer partial reads until a complete message is
  available before parsing.
- R5: WHEN a complete order message is parsed, THE SERVER SHALL
  translate it into a `NewOrder`/cancel call reaching the engine via
  the Phase 4 queue, tagged with enough session/client information to
  route the eventual response back correctly.
- R6: THE SERVER SHALL implement `EventSink` to route `EngineResponse`-
  equivalent notifications back to the originating client's socket.
- R7: Benchmark: round-trip latency (client sends order → server
  parses → engine matches → response serialized → client receives),
  end to end, not just the engine-internal portion measured in Phase 2.

## 3. Non-Functional Requirements

- NFR1: No blocking socket calls anywhere in the I/O thread's hot path.
- NFR2: Connection handling degrades gracefully — a slow or
  disconnected client must not stall other clients' order flow.

## 4. Definition of Done

- Multiple concurrent clients can submit orders and receive correct,
  individually-routed responses.
- Round-trip latency benchmarked and recorded.
- `ClientId` (or equivalent) design decision made explicitly and
  documented (ADR), given the Phase 8 dependency flagged above.

## 5. Open Questions (resolve before design.md for this phase)

1. **Wire format for this phase** — reuse the Phase 1 CLI's plaintext
   grammar (`ADD <id> BUY <price> <qty>`, etc.) over TCP for now, with
   Phase 7 introducing and comparing a binary format afterward? Or is
   it worth defining the binary format now and treating Phase 7 purely
   as "add a JSON comparison for the write-up"? Reusing the plaintext
   grammar is less work now and keeps Phase 7's before/after comparison
   cleaner (plaintext-over-TCP vs. binary vs. JSON, three real data
   points instead of two).
2. **Message framing** — newline-delimited (simple, works fine for a
   text grammar) vs. length-prefixed (works for both text and future
   binary, more "real protocol" shaped)? Leaning length-prefixed since
   it's the pattern Phase 7's binary protocol will need anyway.
3. **`ClientId` introduction** (see flag above) — confirm this is the
   right phase for it, and roughly: is a `ClientId` just "which TCP
   connection," or should it persist across reconnects (implying some
   session/auth concept that doesn't exist yet and may be overkill for
   a portfolio project)?
