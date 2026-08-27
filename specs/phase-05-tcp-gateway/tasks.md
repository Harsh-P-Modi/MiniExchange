# Phase 5 — Tasks: TCP Order Gateway

Status: **APPROVED PLAN — execute one task at a time**

**Execution rule (per `.kiro/steering/structure.md`): do exactly one
task below, then stop and wait for explicit review/approval before
starting the next one.** Update `docs/LEARNING.md` as part of each
task's own review, per `.kiro/steering/learning-doc.md`.

---

## Task 1 — Extract `adapters/text_protocol/`, refactor `apps/cli/` onto it

Move `CLIParser`/`ConsolePrinter`'s logic into
`adapters/text_protocol/TextProtocolParser.hpp` /
`TextProtocolRenderer.hpp` per `design.md` §2. Refactor `apps/cli/` to
depend on this library instead of owning the grammar directly.

**Acceptance criteria:** **every existing Phase 1 CLI test/manual smoke
test still passes unchanged in behavior** — this extraction must be
invisible from the CLI's perspective, same "prove the swap is
invisible" bar as Phase 3's pool swap and Phase 4's `WorkloadEvent`
alias. If any CLI-visible behavior changes, stop and discuss before
continuing.

**Implements:** `requirements.md` §6 (improvement); `design.md` §2.

---

## Task 2 — `core/Types.hpp` addition (`ClientId`) and `core/TaggedCommand.hpp`

Add `ClientId` to `core/Types.hpp`; create `TaggedCommand`/
`TaggedResponse` per `design.md` §3.

**Acceptance criteria:** headers compile; trivial construction/field-
access tests, same bar as Phase 1's Task 2/3 for new small types.

**Implements:** `requirements.md` §5 item 3 (resolved); `design.md` §3.

---

## Task 3 — `adapters/tcp/TcpServer.hpp`: accept + framing, no engine wiring yet

Implement epoll accept loop, `TCP_NODELAY`, length-prefixed framing
with the max-frame-size disconnect behavior, per `design.md` §5. At
this task's scope, parsed frames just get logged/discarded — no queue
wiring yet (that's Task 4), so this task can be tested purely as
"accepts connections, correctly frames/unframes bytes, disconnects on
oversized frames."

**Acceptance criteria (integration test using a real socket client):**
- Multiple concurrent connections accepted and handled independently.
- A message split across multiple TCP packets/reads is correctly
  reassembled before being treated as a complete frame.
- Multiple complete frames arriving in one read are each processed
  (pipelining case).
- An oversized length prefix causes that connection to be disconnected
  without affecting other connected clients (NFR2).

**Implements:** `requirements.md` R1, R2, R4; `design.md` §5 (partial).

---

## Task 4 — Wire `TcpServer` to the two queues + `eventfd` wakeup

Connect parsed frames to `TextProtocolParser` → `TaggedCommand` →
inbound queue (R5); register the outbound queue's `eventfd` in the same
epoll set; on wakeup, drain the outbound `TaggedResponse` queue and
write framed, rendered responses back to the correct client by
`ClientId` (R6, corrected).

**Acceptance criteria:** a synthetic test double stands in for the
engine thread (pops from inbound, pushes a canned `TaggedResponse` onto
outbound) — confirms the TCP thread correctly routes that canned
response back to the exact client that sent the originating command,
even with multiple clients connected simultaneously.

**Implements:** `requirements.md` R5, R6 (corrected); `design.md` §4, §5.

---

## Task 5 — `apps/exchange_server/`

Wire a real `MatchingEngine` on its own thread to the `TcpServer` from
Tasks 3–4, per `design.md` §6.

**Acceptance criteria:** end-to-end test using a real socket client —
submit an order over TCP, receive the correct response over the same
connection; two simultaneous clients each get their own correctly
routed responses and don't see each other's.

**Implements:** `requirements.md` §4 Definition of Done, item 1;
`design.md` §6.

---

## Task 6 — Round-trip latency benchmark (R7)

Measure client-send → parse → engine-match → serialize → client-
receive, end to end, using Phase 2's `LatencyRecorder` pattern.

**Acceptance criteria:** `benchmarks/results/phase-05-tcp-roundtrip.md`
produced; write-up explicitly separates "engine-internal" time (already
known from Phase 2/3's baselines) from "everything TCP adds on top" —
the point of this specific benchmark is isolating the network/framing
overhead, not re-measuring the engine.

**Implements:** `requirements.md` R7.

---

## Task 7 — ADR for `ClientId`

Write `docs/adr/ADR-006-client-id.md` per the Charter's documentation
discipline, since `requirements.md` §4 explicitly calls out this
decision as needing its own ADR (this is the first phase to require a
new ADR beyond Phase 1's original five).

**Acceptance criteria:** ADR exists, follows the established
Context/Decision/Alternatives/Consequences shape.

**Implements:** `requirements.md` §4 Definition of Done, item 3.

---

## Task 8 — Definition of Done audit

Confirm every item in `requirements.md` §4 is met. Confirm
`docs/LEARNING.md` covers the `TaggedCommand`/`TaggedResponse`
two-queue design and the `eventfd` wakeup mechanism in real depth —
both are genuinely non-obvious systems-engineering decisions worth
being able to explain from scratch.

**Acceptance criteria:** Phase 5's Definition of Done fully met.

---

Once Task 8 is signed off, Phase 6 (UDP market data feed) can begin —
it will implement `EventSink` directly against the same live engine,
proving that port's separation from the `TaggedResponse` mechanism
built in this phase.
