# Phase 5 — Tasks: TCP Order Gateway

## Task 0 — Prior-phase backlog cleanup (prerequisite)
**Depends on:** nothing (all existing code is stable and tests pass).
- [ ] Write ADR-003 (single-threaded-per-symbol), ADR-004 (Ports &
      Adapters), ADR-005 (client-supplied lifetime-unique Order IDs) —
      completing Phase 1 Task 15's acceptance criteria. Note: Task 7
      therefore writes only one new ADR (the ClientId one), not four.
- [ ] Add a standalone LEARNING.md entry for Phase 1 Task 5
      (`core/Events.hpp`: `EngineResult`, `EngineResponse`,
      `OrderAccepted`, `OrderCancelled`) — covering why these types
      are separate from `Trade`, why `EngineResponse` bundles
      `vector<Trade>` rather than a count, and why `EngineResult` is
      an enum-class rather than error codes (per `learning-doc.md`'s
      "never skip a module because it seems simple" rule).
- [ ] Add the Phase 1 traceability table (requirement ID → test name)
      required by Task 17's acceptance criteria — same format as the
      Phase 3/4 DoD tables.
- [ ] Add `*.log`, `*.txt` (root only), `buildmpowershell/`,
      `run_bench.bat`, `run_bench.ps1` to `.gitignore`; remove the
      ~25 stray root-level artifacts from tracking and the working
      tree.
- [ ] Update `.github/workflows/ci.yml` clang-tidy `find` command to
      include `lockfree_queue/` and `tools/`. **Going-forward
      convention:** every phase that adds a source directory updates
      the CI scan in the same phase.
- [ ] **Acceptance criteria:** `docs/adr/` contains ADR-001 through
      ADR-005; LEARNING.md has an entry for every Phase 1 task (1–17)
      including the traceability table; repo root has no stray build
      artifacts; CI's clang-tidy scan covers all project source
      directories.

## Task 1 — Extract `adapters/text_protocol/` from `apps/cli/`
**Depends on:** Task 0, Phase 1 CLI complete.
- [ ] Create `adapters/text_protocol/` with `parse()` returning
      only engine-facing commands (`LimitOrder | MarketOrder |
      CancelRequest | ParseError`) — `PRINT_BOOK`/`QUIT` are NOT part
      of the shared layer.
- [ ] Add `render(const EngineResponse&) -> std::string` (no
      `render(const OrderBook&)` — book rendering stays in `apps/cli/`).
- [ ] Refactor `apps/cli/`: intercept `PRINT_BOOK`/`QUIT` locally,
      delegate order/cancel parsing to the shared parser, keep
      `ConsolePrinter`/book rendering in the CLI.
- [ ] All Phase 1 CLI tests still pass after refactor.

## Task 2 — Add `ClientId` to `core/Types.hpp`
**Depends on:** Task 0.
- [ ] Strong wrapper `struct ClientId` (explicit ctor, `==`/`!=`,
      `std::hash` specialization) — same shape as `OrderId`.
- [ ] Unit tests: cannot implicitly construct from / convert to
      `uint64_t`; equality/hash behave.

## Task 3 — TCP server skeleton: listener, epoll loop, connections
**Depends on:** Task 2.
- [ ] `adapters/tcp/` library: `TcpServer` with configurable port.
- [ ] Listener socket, `accept4(SOCK_NONBLOCK)`, `TCP_NODELAY` on
      accepted sockets, `ClientId` assigned per connection.
- [ ] epoll instance, edge-triggered, nonblocking fds; accept loop
      drains until `EAGAIN`.
- [ ] Read path: loop-read until `EAGAIN`, buffer partial frames,
      length-prefix framing (4-byte BE + payload).
- [ ] Write path: per-connection write buffer, flush loops, `EPOLLOUT`
      armed only when data is pending.
- [ ] Connection teardown on `EPOLLHUP`/`EPOLLERR`/EOF.
- [ ] Unit/integration tests:
  - [ ] Framing: partial frame delivered across two `write()`s is
        buffered until complete; two frames in one segment parse as two
        messages.
  - [ ] Multiple concurrent clients each receive only their own
        responses.
  - [ ] The I/O thread correctly handles the edge-triggered "must drain
        completely" invariant: if data arrives while processing is in
        progress (no re-notification), it is still consumed on the same
        iteration — verified by a test that writes a full frame, waits
        for processing to begin, then immediately writes a second frame
        without any new `EPOLLIN` event being guaranteed.
  - [ ] Slow-reader client does not block other clients (NFR2).

## Task 4 — Wire queues: `TaggedCommand` / `TaggedResponse`
**Depends on:** Tasks 2, 3; Phase 4 queue (`lockfree_queue/`).
- [ ] Inbound: `SpscRingBuffer<TaggedCommand, N>`; parse → push.
- [ ] Outbound: `SpscRingBuffer<TaggedResponse, 65536>`; engine thread
      pushes `TaggedResponse` for each command, spin-retries on full
      (R8).
- [ ] `ParseError` → direct in-band text error response, no engine
      round-trip.
- [ ] Response routing: `ClientId` → connection lookup in I/O thread;
      disconnected client's response dropped with debug log.
- [ ] Integration test: multiple clients → engine → correct per-client
      responses; cross-talk test (client A's orders never produce
      responses on client B's socket).

## Task 5 — `apps/exchange_server/` + eventfd wakeup + engine-thread integration
**Depends on:** Task 4.
- [ ] Create `apps/exchange_server/main.cpp` — the composition root
      that wires `MatchingEngine`, the two `SpscRingBuffer` instances,
      `TcpServer`, and `eventfd` together. Spawns the I/O thread,
      runs the engine loop on the main thread. Per design §7.
- [ ] eventfd registered edge-triggered in the epoll set; one write per
      successful outbound push; I/O thread reads counter and drains
      queue to exhaustion on each wakeup.
- [ ] Engine thread loop: spin on inbound, process, push response,
      notify.
- [ ] Shutdown: `SIGINT`/`SIGTERM` → atomic flag set by the signal
      path, then one write to the eventfd to unblock `epoll_wait(-1)`
      (design §5); both threads check the flag and exit; clean join.
- [ ] Integration test: full round trip through real sockets for
      LIMIT/MARKET/CANCEL.
- [ ] Unit test for the R8 spin path: instantiate a deliberately small
      outbound queue (e.g., `SpscRingBuffer<TaggedResponse, 4>`), fill
      it to capacity before the push under test, and verify the
      spin-retry path executes and completes once space frees —
      deterministic, no timing dependence.

## Task 6 — Benchmark
**Depends on:** Task 5.
- [ ] Round-trip latency harness: client → parse → engine → serialize
      → client, end to end.
- [ ] Record with/without `taskset` pinning; record tooling used in
      results (R7).
- [ ] Results appended to benchmark notes with comparison to Phase 2's
      engine-internal numbers.

## Task 7 — ADR (ClientId)
**Depends on:** Task 5 (decision points settled by then); Task 0
(ADR numbering is unambiguous once ADR-003–005 exist).
- [ ] Write `ADR-006-client-id.md`: strong-typed `ClientId`, ephemeral
      per-connection, introduced in Phase 5 for Phase 8/9 consumption.
- [ ] Include queue-sizing and R8 back-pressure rationale in the ADR.
- [ ] Verify `docs/adr/` numbering is contiguous 001–006 with no gaps
      (guaranteed by Task 0; this is the check, not the fix).

## Task 8 — Definition-of-Done sweep
**Depends on:** all above.
- [ ] `apps/cli/` refactor leaves no duplicated grammar logic.
- [ ] All tests green; benchmark recorded; ADR numbering matches
      `docs/adr/` state (requirements §4).
- [ ] Task 0 acceptance criteria still hold at phase close (no new
      stray artifacts introduced by Phase 5 builds — the `.gitignore`
      additions should prevent this; verify).
- [ ] `.kiro/steering/structure.md` conventions respected: new
      libraries in `adapters/` at the repo root, no speculative
      abstraction.
- [ ] `apps/exchange_server/` exists as a buildable target in
      CMakeLists.txt, links against `engine`, `adapters_tcp`,
      `adapters_text_protocol`, and `lockfree_queue` (header-only).
