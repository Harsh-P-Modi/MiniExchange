# MiniExchange — Project Charter

*This is the north star. When unsure whether something belongs in the
project, check here first.*

## Goal

Build a deterministic, low-latency exchange simulator inspired by
modern HFT systems — measured, understood, and improved at every step,
not just "made to work."

## Non-Goals

- No trading strategies meant to be profitable (Phase 10's strategies
  exist only to generate realistic synthetic order flow)
- No machine learning
- No GUI
- No databases / persistence
- No distributed systems, no multi-node consensus
- No regulatory, custody, or settlement concerns — this is a simulator
- No exhaustive protocol coverage (e.g. full FIX spec) — enough to
  demonstrate understanding, not a production gateway

## Core Principles

- Deterministic execution — no wall-clock time, no floating point,
  same input sequence always produces the same output sequence
- Single-threaded matching engine, one instrument per thread (no
  internal locking to make one book "thread-safe" — scale by adding
  more single-threaded books, not by sharing one)
- No floating point, anywhere in `core/`, `orderbook/`, or `engine/`
- Ports & Adapters — `apps/*` and `adapters/*` depend inward on
  `interfaces/`; the engine knows nothing about I/O, sockets, or format
- Allocation-conscious, but only after correctness (Phase 1) and a
  benchmark baseline (Phase 2) exist — never optimize on intuition alone
- Every optimization is benchmarked before and after; keep it only if
  the numbers justify it
- **Never sacrifice correctness for speed.** A correct engine earns the
  right to be optimized. A fast, wrong engine is worthless and
  expensive to debug.

## Performance Targets (directional — refine once Phase 2 exists)

| Phase | Throughput target | Notes |
|---|---|---|
| 1 | Correctness only, no target | Baseline established in Phase 2 |
| 2 | ~100k orders/sec | First real measurement, `std::map` baseline |
| 3 | ~500k+ orders/sec | After memory pool removes heap churn |
| 5+ | 1M+ orders/sec (aspirational) | Once network path is in place |

Latency: track average, median, P99, and worst-case at every phase from
Phase 2 onward — a throughput number without a tail-latency number is
an incomplete story for this kind of system.

## Complexity Goals (checked against every relevant PR)

| Operation | Target |
|---|---|
| Cancel | O(1) amortized |
| Duplicate/unknown ID lookup | O(1) amortized |
| Insert (new price level) | O(log P), P = number of distinct price levels |
| Insert (existing price level) | O(1) — append to intrusive list tail |
| Match | O(number of fills), never more |
| Best bid / best ask | O(1) |

## Coding Standards

- Google C++ Style Guide as the baseline
- `clang-format` enforced, `clang-tidy` enforced
- Build with `-Wall -Wextra -Wpedantic`, warnings treated as errors
- No exceptions for expected business outcomes — see Error Handling

## Logging & Error Handling Policy

- `engine/`, `orderbook/`, `core/` never call `std::cout`, `printf`,
  a logging library, or touch a file/socket — ever. Logging is exclusively
  an `apps/*`/`adapters/*` concern.
- All expected outcomes (duplicate ID, unknown ID, invalid qty/price)
  are returned via `EngineResponse` — no exceptions, no `errno`-style
  side channels. Exceptions are reserved for genuine programming errors
  (invariant violations), not client input.

## Invariants (these become assertions in code, not just comments)

- Order IDs are unique among currently-resting orders.
- Price levels are sorted (bids descending, asks ascending).
- FIFO ordering is preserved within a price level.
- Quantity > 0 and Price > 0 for any resting order.
- After matching completes, best bid < best ask (the book never rests
  in a crossed state).
- Every resting order belongs to exactly one price level.
- The `unordered_map<OrderId, Order*>` index and the intrusive list
  never disagree about what's resting.

## Benchmark Philosophy

Hypothesis → benchmark → optimize → benchmark again → keep or revert.
No optimization lands without a before/after number. No optimization
work happens before Phase 2's baseline exists — Phase 1 is
correctness-first, full stop.

## Documentation Discipline

Every major module's docs answer: what problem it solves, why this
data structure over the alternatives, its complexity, and what
tradeoffs were accepted. Significant decisions also get an ADR in
`docs/adr/` (one page each: Context / Decision / Alternatives
Considered / Consequences) — e.g. "why integer prices, not floating
point," "why intrusive lists, not `std::list`," "why single-threaded
per symbol," "why Ports & Adapters," "why client-supplied Order IDs."
The point: if someone asks "why didn't you use X," the answer is
already written down, not reconstructed from memory.

## Git Discipline

Descriptive, present-tense commit messages describing what changed
(`Implement FIFO matching`, `Replace std::list with custom intrusive
list`, `Add benchmark for cancel latency`) — not `stuff`, `wip`, `fix`.
Recruiters do read commit history.

## Definition of Done (general — each phase's `design.md` refines this)

- All unit tests pass
- Every invariant above holds under test (including fuzz/stress tests
  where applicable)
- No memory leaks (ASan/valgrind clean)
- Benchmarks run and numbers are recorded, compared against the
  previous phase's baseline
- Public API is documented
- A short write-up exists explaining what changed and why

## Prior Art (study architecture, don't copy implementation)

NASDAQ INET, CME Globex, LMAX Exchange (Disruptor pattern), and the
open-source `SimpleOrderbook` and `Liquibook` projects — read for how
they structure the problem, not to lift code.
