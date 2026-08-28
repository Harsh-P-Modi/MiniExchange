# Phase 5 — Requirements: TCP Order Gateway

Status: **APPROVED** — all Open Questions resolved below (including
post-review additions parser command ownership, outbound
back-pressure, ClientId typing, and prior-phase documentation backlog)
— `design.md` and `tasks.md` are built on this version.

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
anywhere yet. **Decision:** introduce `ClientId` here in Phase 5 (as
part of the session/connection model), not wait until Phase 8 — Phase 8
then just consumes a concept that already exists rather than inventing
it under a different phase's spec.

## 2. Functional Requirements (EARS)

- R1: THE SERVER SHALL listen on a configurable TCP port and accept
  multiple concurrent client connections using `epoll` in
  edge-triggered, nonblocking mode.
- R2: THE SERVER SHALL set `TCP_NODELAY` on every accepted client
  socket (disable Nagle's algorithm) — latency over throughput,
  consistent with the project's whole premise.
- R3: THE SERVER SHALL run its I/O loop on a thread separate from the
  matching engine's thread, communicating exclusively through SPSC
  queues in the engine-ward direction.
- R4: Since TCP is a byte stream, not a message stream, THE SERVER
  SHALL implement explicit message framing (length-prefixed, see
  §5.2) and buffer partial reads until a complete message is
  available before parsing.
- R5: WHEN a complete order message is parsed, THE SERVER SHALL
  translate it into an `EngineCommand` (Phase 4) tagged with the
  originating connection's `ClientId`, and push it onto the Phase 4
  queue toward the engine thread.
- R6: THE SERVER SHALL route each `EngineResponse` back to the
  specific client whose command produced it, via a dedicated response
  channel — **not** via `EventSink`. `EventSink` (Phase 1) is a
  broadcast port for "anyone interested in everything that happens"
  (Phase 6's market-data feed, benchmark counters); it has no concept
  of "reply to the specific caller." Routing *your own* response back
  to *you* is a different mechanism: see `design.md` §3 for the
  `TaggedResponse` return queue this actually requires. An adapter
  that wants both its own responses *and* a broadcast feed of everyone
  else's activity uses both mechanisms, not one doing double duty.
- R7: Benchmark: round-trip latency (client sends order → server
  parses → engine matches → response serialized → client receives),
  end to end, not just the engine-internal portion measured in Phase 2.
  Benchmark runs SHALL record whether `taskset`/`numactl` was used;
  stable comparisons SHOULD pin the engine and I/O threads
  consistently. No application-level affinity configuration is
  required in Phase 5.
- R8 (outbound back-pressure): WHEN the outbound `TaggedResponse`
  queue is full, THE ENGINE SHALL spin-retry until space is available,
  and the queue capacity SHALL be sized generously enough that this is
  not reachable under normal operation. **Accepted limitation:** a
  pathologically slow I/O thread can therefore stall the engine thread;
  this mirrors the bounded-memory priority of Phase 4 (inbound: reject)
  while preserving request/response semantics (responses are never
  dropped silently). Documented in ADR alongside the queue sizing
  rationale. Revisit only if profiling shows exhaustion in practice.

## 3. Non-Functional Requirements

- NFR1: No blocking socket calls anywhere in the I/O thread's hot path.
- NFR2: Connection handling degrades gracefully — a slow or
  disconnected client must not stall other clients' order flow (the
  engine stall risk from R8's extreme case is a bounded, accepted
  tradeoff, not the common path).

## 4. Definition of Done

- Prior-phase documentation backlog cleared (Task 0): ADR-001 through
  ADR-005 exist in `docs/adr/`; LEARNING.md has an entry for every
  Phase 1 task (1–17), including the standalone Phase 1 Task 5 entry
  and the R-number→test-name traceability table; repo root carries no
  stray build/log artifacts; CI's clang-tidy scan covers all project
  source directories (`lockfree_queue/` and `tools/` included).
- Multiple concurrent clients can submit orders and receive correct,
  individually-routed responses.
- Round-trip latency benchmarked and recorded (with thread-pinning /
  tooling notes per R7).
- `ClientId` design decision made explicitly and documented, given the
  Phase 8 dependency flagged above (see §5.3).
- Phase 5's ADR number matches the actual `docs/adr/` state — no gap
  between the plan and the repository.

## 5. Open Questions — Resolved

1. **Wire format — RESOLVED: reuse Phase 1's CLI plaintext grammar
   over TCP for this phase.** Phase 7 introduces and compares binary
   separately, keeping that comparison clean (plaintext-over-TCP vs.
   binary vs. JSON — three real data points instead of two).
2. **Message framing — RESOLVED: length-prefixed** (4-byte big-endian
   length + payload). Works for both this phase's text grammar and
   Phase 7's future binary payloads, unlike newline-delimited framing.
3. **`ClientId` — RESOLVED: introduced here, one per accepted TCP
   connection**, implemented as a **strong wrapper struct**
   (explicit constructor + comparison operators), consistent with the
   existing `OrderId`/`Price`/`Quantity`/`Sequence` convention — no
   bare `using ClientId = uint64_t;` alias, which would let
   `ClientId`s and `OrderId`s interchange unnoticed. Ephemeral, tied
   to the socket/connection, not persisted across reconnects — no
   session/auth concept, which this project doesn't need. Lives in
   `core/Types.hpp` (a fundamental type, even though this phase is
   what first requires it) since Phase 8 and Phase 9 both consume it
   later.
4. **Shared parser: `PRINT_BOOK` / `QUIT` ownership — RESOLVED: these
   remain CLI-only commands.** `adapters/text_protocol/` exposes a
   parse-result type covering only engine-facing commands (`LimitOrder`,
   `MarketOrder`, `CancelRequest`) plus `ParseError`; the CLI keeps its
   richer result type and intercepts `PRINT_BOOK` / `QUIT` before (or
   around) invoking the shared parser. Consequence: the TCP gateway can
   never receive an unrepresentable command, `render(const OrderBook&)`
   stays in `apps/cli/`, and the shared protocol layer does not depend
   on the `OrderBook` domain component. If a later phase wants
   human-readable book dumps over another adapter, it extends the
   shared layer then — deliberately, not accidentally.
5. **Outbound notification semantics — RESOLVED: one `eventfd` write
   per successful outbound queue push; the I/O thread drains the queue
   to exhaustion (`try_pop()` returning false) on each epoll wakeup.**
   This batches naturally without requiring one notification per
   response. Exact pattern specified in `design.md`.

## 6. Phase 5 Scope Limits (Explicit Simplifications)

These are conscious boundaries, decided up front so implementation
doesn't have to:

- **Graceful shutdown:** `SIGINT`/`SIGTERM` sets an atomic shutdown
  flag observed by both threads at their next loop iteration; active
  connections are closed without attempting a graceful response drain.
  No production-grade sequencing.
- **Per-connection write-buffer cap:** none imposed in Phase 5. An
  accepted simplification; production deployment would require bounded
  buffering plus a back-pressure/disconnect policy. An unresponsive
  client whose write buffer grows without bound could ultimately fill
  the outbound SPSC queue and trigger R8's engine-thread spin — this
  is the accepted pathological case for a portfolio piece without
  connection-level flow control.
- **Half-closed connections:** once the client has half-closed its
  write side, no new inbound commands are accepted from it. Pending
  outbound responses may be flushed if the connection remains
  writable; otherwise the connection is closed without guaranteeing
  delivery.
- **Idle connections:** no idle timeout in Phase 5; idle clients may
  remain connected indefinitely.
- **Allocation in responses:** `EngineResponse` carries a heap
  -allocated `std::vector<Trade>`; moves through the SPSC queue
  transfer ownership without copying payloads, but allocation/
  deallocation remains a potential latency source. Accepted for this
  correctness-first phase; revisit (fixed-capacity inline storage) in
  the binary-protocol phase.

## 7. Prior-Phase Backlog (Resolved Alongside Phase 5)

A repo audit surfaced documentation/hygiene debt from Phases 1–4 that
is functionally invisible but would be noticed in a careful review.
Rather than let it accumulate, Phase 5 clears it as a bounded,
explicit prerequisite (Task 0 in `tasks.md`) — not as implicit debt
that the ADR task partially catches:

1. **Missing ADRs (Phase 1 Task 15 incomplete):** only ADR-001 and
   ADR-002 exist; ADR-003 (single-threaded-per-symbol), ADR-004
   (Ports & Adapters), and ADR-005 (client-supplied lifetime-unique
   Order IDs) were planned but never written.
2. **Missing LEARNING.md entry for Phase 1 Task 5** (`core/Events.hpp`:
   `EngineResult`, `EngineResponse`, `OrderAccepted`,
   `OrderCancelled`) — per `learning-doc.md`'s "never skip a module
   because it seems simple" rule, it needs its own entry covering why
   these types are separate from `Trade`, why `EngineResponse` bundles
   `vector<Trade>` rather than a count, and why `EngineResult` is an
   enum-class rather than error codes.
3. **Missing Phase 1 traceability table** (Task 17's acceptance
   criteria: requirement ID → test name). Phases 3 and 4 have explicit
   tables; Phase 1 does not.
4. **Stray build/log artifacts at the repo root** (~25 files:
   `bench_*.txt`, `build_*.log`, `cmake_output.log`, `test_*.log`,
   `run_bench.bat`/`.ps1`, `buildmpowershell/`, etc.) plus `.gitignore`
   gaps that let them accumulate.
5. **CI clang-tidy scan gaps:** the `find` command in
   `.github/workflows/ci.yml` omits `lockfree_queue/` (Phase 4) and
   `tools/` (Phase 2). Going forward, each phase that adds a source
   directory updates the scan — a convention, not a one-off fix.

All are documentation/hygiene items; no functional code changes are
required by this section.

## 8. Improvement — extracting the shared plaintext-grammar parser

Phase 1's `CLIParser`/`ConsolePrinter` live entirely inside
`apps/cli/`, per the rule in `.kiro/steering/structure.md`: "extract to
`adapters/` only once something is actually reused, not
speculatively." That trigger has now arrived — this phase needs the
*same* ADD/CANCEL/MARKET grammar over TCP. Rather than duplicate the
parser/renderer inside `adapters/tcp/`, this phase extracts them into a
new `adapters/text_protocol/` library that both `apps/cli/` (refactored)
and `adapters/tcp/` depend on. Per §5.4, the extraction covers only
engine-facing commands: `PRINT_BOOK` rendering stays in `apps/cli/`,
as do `QUIT` handling and CLI lifecycle concerns. See `design.md` §2
for the exact shape.
