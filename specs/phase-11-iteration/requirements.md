# Phase 11 — Requirements: Iteration

Status: **RESOLVED — open questions settled below; approve this before `design.md`**

## 1. Scope

This phase is not a new feature. It is a remediation pass against the
running system, driven by two artifacts produced after Phase 10 shipped:

1. A **measured, source-level latency teardown** ("the research report") —
   three instrumented probes linked against the real `MatchingEngine`,
   `RiskEngine`, and `SpscRingBuffer`, plus a source trace of every hot
   path.
2. An **adversarial self-review of that report** ("the adversarial
   review") — attacking every recommendation in it for cargo-cult
   reasoning, unsupported magnitude claims, hardware the project doesn't
   have, and internal contradictions.

Only findings that survived *both* passes are in scope here. A finding
had to clear two bars:

- The **mechanism** is certain from reading the shipped code (not
  inferred from a budget estimate), or the **magnitude** is measured on
  a metric that isn't quantization-limited (allocation counts, aggregate
  throughput — not single-op medians near the ~100 ns timer floor).
- It sits on the **real hot path** of this project (order-to-ack or
  order-to-feed), not a hypothetical one (this project is a venue, not
  a tick-to-trade strategy pipeline — see the research report §1).

Everything that failed either bar is listed in §6 as explicitly **out of
scope**, with the reason, so a future reader doesn't rediscover and
re-litigate it.

## 2. Functional Requirements (EARS)

### Correctness (no benchmark needed — mechanism alone is the argument)

- R1: WHEN the inbound queue is full, THE SYSTEM SHALL send the
  submitting client an explicit rejection response instead of silently
  discarding the order. Today `main.cpp`'s frame handler calls
  `inbound.try_push(std::move(cmd))` and ignores a `false` return — the
  order vanishes with no notification to the client
  (`apps/exchange_server/main.cpp:217-219`). The frame handler already
  has a direct-response path for protocol-level errors
  (`server.send_to_client(client_id, frame_message(err_msg))` at
  `main.cpp:210-212`); reuse it.
- R2: THE OUTBOUND RESPONSE SHALL carry the `OrderId` of the order it
  answers, so a client that has more than one order in flight can
  correlate a response to the request that produced it.
  `EngineResponse` (`core/Events.hpp`) currently carries only `status`,
  `trades`, and `remaining_qty` — no `OrderId`. (Confirmed against the
  binary/text gateway codecs, which are forced to synthesize a
  placeholder order id in the ack/reject they render, because the real
  one was never threaded through.)
- R3: WHEN the engine thread's per-command dispatch throws, THE SYSTEM
  SHALL catch it at the loop boundary, and SHALL NOT allow an
  uncaught exception to terminate the process. Today
  `main.cpp`'s engine loop (`main.cpp:249-296`) has no exception
  handling around `engine.submit(...)`/`engine.cancel(...)`; any throw
  is an uncaught `std::terminate` that kills the whole venue for every
  connected client, not just the one whose input triggered it.
- R4: THE SYSTEM SHALL bound each connection's outbound write buffer
  (`Connection::write_buffer`, `adapters/tcp/tcp_server.hpp:38`) to a
  configured maximum size, and WHEN a client's buffer would exceed that
  bound, THE SYSTEM SHALL close that connection rather than let the
  buffer grow without limit. Today `send_to_client`
  (`adapters/tcp/tcp_server.cpp:404-414`) appends unconditionally.

### Latency: syscalls off the engine thread's stack

- R5: THE MATCHING ENGINE SHALL NOT execute blocking socket I/O
  synchronously on its own call stack as a side effect of matching.
  Today `sink_->on_trade(trade)` is called directly from
  `match_against_book` (`engine/matching_engine.cpp:260`), and in the
  production composition root `sink_` is `UdpFeedPublisher`, whose
  `on_trade`/`on_order_accepted`/`on_order_cancelled` handlers call
  `sendto()`. An order crossing several resting orders therefore drives
  several synchronous `sendto()` calls before `submit()` returns to its
  caller — inside the one call the project's own steering rule
  ("the engine performs zero I/O", `.kiro/steering/tech.md`) forbids
  I/O in. R5 requires decoupling publication from the matching call
  stack — a queue-backed event sink consumed by a separate thread — not
  merely moving the `sendto` elsewhere inside the same call.
- R6: THE ENGINE THREAD SHALL coalesce its wakeup notification to the
  I/O thread to at most one `write(g_eventfd, ...)` per outbound-queue
  drain cycle, not one per response. Today `main.cpp:297-298` issues a
  `write()` to the eventfd after every single response is pushed,
  inside the per-command loop body — one syscall per order regardless
  of how many responses are already queued.

### Latency / memory: state that grows or costs with book state

- R7: `ever_seen_ids_` (`engine/matching_engine.hpp:51`,
  `std::unordered_set<OrderId>`) SHALL be replaced with a bounded
  structure. Lifetime-unique-ID validation is still required
  (`ever_seen_ids_.contains(order.id)` at
  `engine/matching_engine.cpp:66,129`); the *implementation* must stop
  retaining state forever for orders that have already been fully
  cancelled or filled out of the book.
- R8: `MatchingEngine::would_self_cross`
  (`engine/matching_engine.cpp:285`, declared
  `engine/matching_engine.hpp:82`) SHALL run in time independent of
  book depth. Today it is called from both `submit_limit`
  (`engine/matching_engine.cpp:76`) and `submit_market`
  (`:138`) as a pre-scan of the crossable opposite side, and its cost
  scales with how many levels/orders that scan visits before finding
  (or failing to find) a same-owner resting order.

### Verification

- R9: THE PHASE SHALL produce a **before/after syscall-count
  comparison** (`perf trace -s` or equivalent) on a real Linux run,
  demonstrating that R5 and R6 reduced syscalls issued per filled
  order. This is the only claim in either report that is falsifiable
  in minutes on real hardware, and it is the number this phase is
  judged against — not a synthetic single-process latency harness.

## 3. Non-Functional Requirements

- NFR1: No task in this phase SHALL regress the existing test suite
  (286 tests at Phase 10 baseline). Every change ships with its own
  new tests, matching every prior phase's discipline.
- NFR2: Every latency-motivated change (R5–R8) SHALL be measured and
  recorded against its own documented baseline — extending Phase 8's
  "benchmark numbers vs baseline" convention
  (`benchmarks/results/phase-08-order-size.md`) — rather than asserted
  from the research report's numbers. The research report's numbers
  were measured on an unpinned Windows dev box with ~100 ns clock
  granularity (its own stated caveat) and are a hypothesis to confirm
  here, not a result to cite as this phase's evidence.
- NFR3: None of R1–R8 SHALL introduce a new heap allocation on the
  steady-state hot path. Several (R7, R8) exist specifically to
  *remove* unbounded or O(depth) cost; they must not trade that for a
  different hot-path allocation.
- NFR4: A correctness fix (R1–R4) SHALL NOT be blocked on, or bundled
  with, a latency fix (R5–R9). They are independently shippable and
  are sequenced that way in `tasks.md` — correctness first, because it
  needs no measurement to justify and no hardware to verify.

## 4. Definition of Done

- R1–R9 implemented and covered by tests, including the specific
  failure modes each one fixes (queue-full rejection is observed by
  the client; a pipelined pair of orders is correlated by `OrderId`; a
  deliberately-thrown exception during dispatch does not kill the
  process; a slow-reading client is disconnected once its buffer
  bound is exceeded; STP cost at depth 1000 is no longer ~50× depth-1
  cost).
- R7's replacement structure is exercised by the same 200,000
  add+cancel-cycle stress shape the research report used, with the
  retained-allocation count recorded and compared (expectation:
  bounded, not growing linearly with cycle count).
- R9's `perf trace -s` before/after comparison is captured on real
  Linux and stored under `benchmarks/results/phase-11-*` alongside
  reproduction steps, following the Phase 8 precedent of documenting
  what could and couldn't be run in this dev environment.
- `docs/LEARNING.md` updated per steering, one entry per task.

## 5. Resolved Open Questions

1. **Feed decoupling (R5) — what "decoupled" means, resolved.**
   Moving the `sendto()` call to a different line inside the same
   synchronous call from `match_against_book` would not satisfy R5;
   the requirement is that the *engine's call stack* returns from
   `submit()`/`cancel()` without having waited on a socket. The
   decorator/port shape stays (`EventSink` remains the interface
   `MatchingEngine` calls); what changes is that the concrete consumer
   wired into the composition root for `UdpFeedPublisher` traffic
   publishes into a queue instead of calling `sendto` directly, and a
   separate thread drains that queue and does the I/O. See `design.md`
   §5 for why this is a queue, not a rename of the existing `EventSink`
   port.

2. **`ever_seen_ids_` replacement structure (R7) — resolved.** Of the
   three options the adversarial review weighed (bounded ring + Bloom
   filter, session-scoped IDs, per-client monotonic watermark), this
   phase adopts the **per-client monotonic watermark**: reject any
   `OrderId` at or below the highest ID previously accepted for that
   `ClientId`. It is O(1) memory per client, O(1) per check, and
   requires no allocation on the hot path (NFR3). This is a
   **semantic narrowing** from today's "lifetime-unique across all
   clients" to "monotonically increasing per client" — see §6 for the
   compatibility note this requires.

3. **STP O(1) mechanism (R8) — resolved.** Add a per-client index of
   currently-resting order prices (or, more narrowly, each client's
   best resting bid / worst resting ask), maintained incrementally on
   insert/remove — both already O(1) operations — so
   `would_self_cross` becomes a lookup and a price comparison instead
   of a walk of the crossable side. This is the "per-client resting-order
   index" option from the adversarial review, chosen over the
   per-level-owner-count alternative because it is the one that is
   genuinely O(1) rather than O(levels).

4. **Response payload flattening (POD `EngineResponse`/cache-index
   SPSC caching) — explicitly deferred, not included in this phase.**
   Both were measured (4× queue cost; 40–69% ring throughput) but both
   are secondary-order changes (~2% and low-tens-of-ns respectively)
   relative to R5/R6/R8, and R10's payload change interacts with every
   consumer of `EngineResponse`/`TaggedResponse` including the FIX and
   binary codecs. Rather than bundle a wide, low-yield change into a
   phase whose actual argument is "delete the syscalls," these are
   listed in §6 as **candidates for a follow-up phase**, not dropped —
   the measurements from both reports remain valid evidence for that
   future phase.

5. **Two-thread vs. one-thread architecture — explicitly deferred, not
   a task in this phase.** Both reports flag this as the most
   interesting open question and both flag that neither report
   actually settled it (the research report speculated a
   single-thread design could win; the adversarial review pointed out
   the speculation depended on core isolation this project's dev
   environment doesn't have, and that isolation itself changes the
   answer). This phase does not require a production deployment
   target or isolated cores to complete R1–R9. Resolving this question
   is out of scope here — see §6.

## 6. Out of Scope (with reason)

Every item below appeared as a recommendation in the research report
and was either withdrawn or downgraded by the adversarial review. Listed
here so nobody re-adds it to this phase without re-deriving the reason.

| Item | Why it's out |
|---|---|
| io_uring | Removes the syscall *boundary*, not the TCP stack traversal; R5/R6 remove more syscalls per order than io_uring would on top of them. Revisit only after R5/R6/R9 land and a real bottleneck remains in the transport layer itself. |
| Onload / kernel bypass NICs | Requires Solarflare-class hardware this project does not have. Aspirational, not actionable here. |
| Huge pages / `mlockall` | No dTLB-miss measurement exists to justify it (the adversarial review's own point) — the 72 MB order pool's *working set*, not its allocation size, is what would matter, and it's unmeasured. |
| Core isolation (`isolcpus`/`nohz_full`) / busy-spin without isolation | Requires a dedicated Linux host with reservable cores. Busy-spin is explicitly **not** to be added independently of isolation — on shared/non-isolated cores it makes tail latency worse, not better (adversarial review finding). |
| Disruptor-style multi-consumer ring | The two consumers this project actually needs (journal, feed) want *different* streams (engine input vs. engine output); adopting the named pattern is solving a larger problem than R5 has. |
| Full pre-trade risk suite (position/notional/rate limits, kill switch) | Real venue functionality, but scope expansion, not a latency or correctness fix, and conflicts with `product.md`'s "this is a simulator" scope statement. Candidate for a distinct future phase, not this one. |
| Sequencer + journal + deterministic replay | Genuinely valuable (§16 of the research report) but *adds* work to the critical path — it is a capability phase, not a remediation phase, and must not be filed alongside latency fixes as if it were one. Candidate for Phase 12. |
| DPDK, FPGA feed handler, custom allocator, SIMD in the match loop, replacing `std::map` with a flat price ladder | All identified as cargo-cult for this project's traffic scale and data-access pattern in the adversarial review — either solving a problem this project doesn't have (packet rate, cold-path latency) or optimizing a component that is already ~1–2% of the measured budget. |
| PGO / LTO / BOLT | The cited improvement figures were unsupported guesses in the original report ("5–15%" with no source); not ruled out forever, just not a claim this phase makes. |

## 7. Cross-Cutting Impact: semantics of duplicate-ID rejection

R7 changes duplicate-ID rejection from **global lifetime-uniqueness**
(no `OrderId` may ever repeat across any client, for the life of the
process) to **per-client monotonic uniqueness** (each client's own
`OrderId`s must strictly increase; different clients may reuse the same
numeric ID). This is a narrower guarantee than Phase 1's original
requirement, and:

- `EngineResult::DuplicateOrderId` (`core/Events.hpp`) is reused for
  the new check's rejection — the wire contract (which result code a
  client sees) does not change.
- Existing tests that submit the same `OrderId` from *different*
  `ClientId`s to prove global rejection will need to be updated to
  reflect the new per-client scope; this is a deliberate, documented
  behavior change, not a regression, and `tasks.md` calls it out as
  its own reviewable step.
- Client-facing documentation/protocol notes (if any describe
  lifetime-global uniqueness) must be updated in the same task.
