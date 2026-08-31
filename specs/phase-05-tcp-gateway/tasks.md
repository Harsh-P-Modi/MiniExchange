# Phase 5 — Tasks: TCP Order Gateway

Status: **COMPLETE** — all tasks below implemented and test-verified;
see `README.md`'s phase status table and `docs/LEARNING.md`'s Phase 5
section for verification detail. (Task 7's round-trip latency numbers
in `benchmarks/results/phase-05-tcp-roundtrip.md` are recorded as
placeholders pending a controlled Linux run — see that file's
"Environment requirements" section — same caveat as later phases'
Linux-only benchmarks.)

## Overview

TCP order gateway for MiniExchange: extract a reusable text protocol adapter, build an epoll-based TCP server with length-prefix framing, wire inbound/outbound SPSC queues between network and engine threads, compose into an `exchange_server` app, benchmark round-trip latency, and clean up prior-phase documentation debt.

## Tasks

- [x] 1. Prior-phase backlog cleanup (prerequisite)
  - [x] 1.1. Write ADR-003 (single-threaded-per-symbol), ADR-004 (Ports & Adapters), ADR-005 (client-supplied lifetime-unique Order IDs)
  - [x] 1.2. Add a standalone LEARNING.md entry for Phase 1 Task 5 (`core/Events.hpp`)
  - [x] 1.3. Add the Phase 1 traceability table (requirement ID → test name)
  - [x] 1.4. Add stray artifacts to `.gitignore`; remove ~25 stray root-level artifacts from tracking
  - [x] 1.5. Update `.github/workflows/ci.yml` clang-tidy `find` command to include `lockfree_queue/` and `tools/`
  - [x] 1.6. Verify acceptance criteria: `docs/adr/` contains ADR-001 through ADR-005; LEARNING.md has entry for every Phase 1 task; repo root has no stray build artifacts; CI clang-tidy covers all source dirs
- [x] 2. Extract `adapters/text_protocol/`, refactor `apps/cli/` onto it
  - [x] 2.1. Create `adapters/text_protocol/` with `parse()` returning only engine-facing commands
  - [x] 2.2. Add `render(const EngineResponse&) -> std::string`
  - [x] 2.3. Refactor `apps/cli/`: intercept PRINT_BOOK/QUIT locally, delegate to shared parser
  - [x] 2.4. Verify all Phase 1 CLI tests still pass after refactor
- [x] 3. Add `ClientId` to `core/Types.hpp`
  - [x] 3.1. Implement strong wrapper `struct ClientId` with explicit ctor, ==, !=, std::hash
  - [x] 3.2. Unit tests: cannot implicitly construct/convert, equality/hash behave
- [x] 4. TCP server skeleton: listener, epoll loop, connections
  - [x] 4.1. Create `adapters/tcp/` library with TcpServer and configurable port
  - [x] 4.2. Listener socket, accept4(SOCK_NONBLOCK), TCP_NODELAY, ClientId per connection
  - [x] 4.3. epoll edge-triggered, nonblocking fds; accept loop drains until EAGAIN
  - [x] 4.4. Read path: loop-read until EAGAIN, buffer partial frames, length-prefix framing (4-byte BE + payload)
  - [x] 4.5. Write path: per-connection write buffer, flush loops, EPOLLOUT armed only when data pending
  - [x] 4.6. Connection teardown on EPOLLHUP/EPOLLERR/EOF
  - [x] 4.7. Integration tests for framing, multiple clients, edge-triggered drain, slow-reader
- [x] 5. Wire queues: TaggedCommand / TaggedResponse
  - [x] 5.1. Define TaggedCommand and TaggedResponse structs with ClientId
  - [x] 5.2. Inbound: SpscRingBuffer<TaggedCommand, N>; parse → push
  - [x] 5.3. Outbound: SpscRingBuffer<TaggedResponse, 65536>; engine spin-retries on full (R8)
  - [x] 5.4. ParseError → direct in-band text error response, no engine round-trip
  - [x] 5.5. Response routing: ClientId → connection lookup; disconnected client's response dropped
  - [x] 5.6. Integration test: multiple clients → engine → correct per-client responses; cross-talk test
- [x] 6. `apps/exchange_server/` + eventfd wakeup + engine-thread integration
  - [x] 6.1. Create apps/exchange_server/main.cpp composition root
  - [x] 6.2. eventfd registered edge-triggered in epoll; one write per outbound push; drain to exhaustion
  - [x] 6.3. Engine thread loop: spin on inbound, process, push response, notify
  - [x] 6.4. Shutdown: SIGINT/SIGTERM → atomic flag + eventfd write → clean join
  - [x] 6.5. Integration test: full round trip through real sockets for LIMIT/MARKET/CANCEL
  - [x] 6.6. Unit test for R8 spin path with deliberately small queue
- [x] 7. Benchmark
  - [x] 7.1. Round-trip latency harness: client → parse → engine → serialize → client
  - [x] 7.2. Record with/without taskset pinning; record tooling used
  - [x] 7.3. Append results to `benchmarks/results/`
- [x] 8. ADR (ClientId)
  - [x] 8.1. Write ADR-006-client-id.md
  - [x] 8.2. Include queue-sizing and R8 back-pressure rationale
  - [x] 8.3. Verify docs/adr/ numbering is contiguous 001–006
- [x] 9. Definition-of-Done sweep
  - [x] 9.1. Verify apps/cli/ refactor leaves no duplicated grammar logic
  - [x] 9.2. All tests green; benchmark recorded; ADR numbering matches
  - [x] 9.3. Task 1 acceptance criteria still hold at phase close
  - [x] 9.4. .kiro/steering/structure.md conventions respected
  - [x] 9.5. apps/exchange_server/ exists as a buildable CMake target

## Task Dependency Graph

```json
{
  "waves": [
    { "tasks": [1] },
    { "tasks": [2, 3] },
    { "tasks": [4] },
    { "tasks": [5] },
    { "tasks": [6] },
    { "tasks": [7, 8] },
    { "tasks": [9] }
  ],
  "dependencies": {
    "2": [1],
    "3": [1],
    "4": [3],
    "5": [3, 4],
    "6": [5],
    "7": [6],
    "8": [1, 6],
    "9": [1, 2, 3, 4, 5, 6, 7, 8]
  }
}
```

## Notes

- Task 1 maps to the "prior-phase backlog cleanup" that unblocks clean ADR numbering and documentation completeness for subsequent phases.
- Task 4 depends on Task 3 (needs ClientId for per-connection identification).
- Task 5 depends on the Phase 4 lock-free SPSC ring buffer being available and stable.
- The R8 requirement (engine spin-retries when outbound queue is full) is tested explicitly in Task 6.6 with a deliberately undersized queue.
