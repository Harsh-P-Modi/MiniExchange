# Phase 5 — Requirements: TCP Order Gateway

Status: **APPROVED** — Open Questions resolved below; R6 also
corrected (see below) — `design.md` and `tasks.md` are built on this
version.

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
  translate it into an `EngineCommand` (Phase 4) tagged with the
  originating connection's `ClientId`, and push it onto the Phase 4
  queue toward the engine thread.
- R6 (corrected — see note below): THE SERVER SHALL route each
  `EngineResponse` back to the specific client whose command produced
  it, via a dedicated response channel — **not** via `EventSink`.
  `EventSink` (Phase 1) is a broadcast port for "anyone interested in
  everything that happens" (Phase 6's market-data feed, benchmark
  counters); it has no concept of "reply to the specific caller," and
  nothing about it changes here. Routing *your own* response back to
  *you* is a different mechanism: see `design.md` §3 for the
  `TaggedResponse` return queue this actually requires. (This
  correction matters because the original wording would have led to
  conflating two ports that need to stay separate — an adapter that
  wants both its own responses *and* a broadcast feed of everyone
  else's activity needs to use both mechanisms, not one doing double
  duty.)
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

## 5. Open Questions — Resolved

1. **Wire format — RESOLVED: reuse Phase 1's CLI plaintext grammar
   over TCP for this phase.** Phase 7 introduces and compares binary
   separately, keeping that comparison clean (plaintext-over-TCP vs.
   binary vs. JSON — three real data points instead of two).
2. **Message framing — RESOLVED: length-prefixed** (4-byte big-endian
   length + payload). Works for both this phase's text grammar and
   Phase 7's future binary payloads, unlike newline-delimited framing.
3. **`ClientId` — RESOLVED: introduced here, one per accepted TCP
   connection.** Ephemeral, tied to the socket/connection, not
   persisted across reconnects — no session/auth concept, which this
   project doesn't need. Lives in `core/Types.hpp` (a fundamental type,
   even though this phase is what first requires it) since Phase 8 and
   Phase 9 both consume it later.

## 6. Improvement — extracting the shared plaintext-grammar parser

Phase 1's `CLIParser`/`ConsolePrinter` live entirely inside
`apps/cli/`, per the rule in `.kiro/steering/structure.md`: "extract to
`adapters/` only once something is actually reused, not
speculatively." That trigger has now arrived — this phase needs the
*exact same* ADD/CANCEL/MARKET/PRINT_BOOK grammar over TCP. Rather than
duplicate the parser/renderer inside `adapters/tcp/`, this phase
extracts them into a new `adapters/text_protocol/` library that both
`apps/cli/` (refactored) and `adapters/tcp/` depend on. See `design.md`
§2 for the exact shape.
