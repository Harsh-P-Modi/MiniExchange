# Product

## What this is

MiniExchange — a from-scratch, benchmarked, production-quality limit
order book and matching engine, built to demonstrate systems-engineering
judgment to HFT/prop-trading recruiters (Optiver, Tower, IMC, Graviton,
Quadeye, AlphaGrep, NK Securities, etc.), not to make money.

## Why this exists

A GitHub project that "just works" is not the goal. The goal is a
repository where every design decision is justified: why this data
structure over another, why this memory layout, why this optimization
reduces latency, what the benchmark numbers show, what tradeoffs were
accepted. That narrative — measured, understood, improved — is what a
recruiter spending five minutes on the repo should come away with.

## What "done" looks like, per phase

Not just passing tests. Each phase ships with:
- Correctness tests (GoogleTest) covering every requirement.
- Benchmark numbers recorded in `benchmarks/`, compared against the
  previous phase's baseline where relevant (see Phase 2 onward).
- A short README/doc write-up of what changed and why, with numbers.

## Guiding architecture philosophy

"The core is a deterministic, single-threaded, allocation-conscious
matching engine. Everything else is an adapter or an app." Any time
networking, logging, file I/O, or formatting is about to be added
inside `engine/`, `orderbook/`, or `core/` — stop and ask whether it's
business logic or an app/adapter's job. It's almost always the
app/adapter's job.

## Non-goals

- Not a real exchange; no regulatory, custody, or settlement concerns.
- Not optimizing to make the strategies (Phase 10) profitable — they
  exist only to generate realistic synthetic order flow.
- Not chasing exhaustive protocol coverage (e.g. full FIX spec) — enough
  to demonstrate understanding, not a production gateway.

## Build order (do not reorder without updating this file)

1. Limit order book + matching engine (single-threaded, single symbol)
2. Benchmark harness + baseline numbers
3. Memory pool (remove heap churn)
4. Lock-free queue (network thread → matching thread)
5. TCP order gateway (epoll, nonblocking, TCP_NODELAY)
6. UDP market data feed (multicast simulation)
7. Binary wire protocol (vs JSON baseline)
8. Risk engine (position limits, price bands, fat-finger checks)
9. Minimal FIX parser (35=D/F/8)
10. Strategy SDK (synthetic order flow generators)

Each phase is spec'd (`requirements.md` → `design.md` → `tasks.md`) and
approved before code is written. No phase starts until the previous
phase's benchmarks are recorded.
