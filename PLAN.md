# MiniExchange — Master Plan

*(working name — rename freely once you pick something)*

## Philosophy

This project is built **spec-first, phase-gated**, the same way you ran the
FixMyPlant Kiro workflow: for every phase we produce

```
requirements.md  → what the phase must do (EARS-style "the system shall...")
design.md        → data structures, algorithms, why-not-alternatives
tasks.md         → ordered, checkable implementation steps
benchmarks.md    → what gets measured, target numbers, how it's measured
```

and you approve each file before code gets written. Nothing moves to
"code" until `design.md` is signed off. No phase starts until the
previous phase's Definition of Done is met — which includes recorded
benchmarks only for phases whose own spec calls for them (Phase 1
explicitly doesn't; Phase 2 onward generally does). Don't read
"benchmarks" as a universal gate that would make Phase 2 impossible to
start.

**Ground rule for me (Claude) in this project:** I do not write the whole
engine unsupervised. I explain tradeoffs, review your code for correctness/
cache behavior/allocation, help design benchmarks, and write code only for
pieces we've explicitly scoped in a `tasks.md`. If I ever produce something
like a lock-free structure, treat it as a first draft that needs your own
correctness reasoning (TSan/ASan runs, a stress test) — not a finished
component.

## Architecture (hexagonal, ports & adapters, event-driven output)

```
   ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐
   │  CLI   │  │ Replay │  │Benchmk │  │  (later)│  ← apps/ (executables,
   └────┬───┘  └───┬────┘  └───┬────┘  └───┬────┘     composition roots)
        │          │          │          │
   ┌────┴───┐  ┌────┴───┐  ┌────┴───┐
   │  TCP   │  │  FIX   │  │  UDP   │                ← adapters/ (reusable
   └────┬───┘  └───┬────┘  └───┬────┘                   translation libs)
        └──────────┴──────┬────┴───────────
                           ▼
            ============ interfaces/ (PORTS) ============
              EngineAPI (input port: submit / cancel)
              EventSink (output port: on_trade / on_accept /
                         on_cancel — Observer, kept lightweight)
            ================================================
                           ▼
                  ┌─────────────────────┐
                  │      engine/         │   MatchingEngine
                  │  (no I/O, no threads,│
                  │   single-threaded    │
                  │   per symbol)        │
                  └──────────┬───────────┘
                             ▼
                  ┌─────────────────────┐
                  │    orderbook/        │   OrderBook, PriceLevel
                  └──────────┬───────────┘
                             ▼
                  ┌─────────────────────┐
                  │      core/           │   Order, Trade, Side, Price,
                  │  (pure domain types) │   OrderId, Sequence, Events
                  └─────────────────────┘
```

**`apps/` vs `adapters/` — different roles, kept separate:** an adapter
is a reusable library translating between the outside world and a port
(e.g. `adapters/tcp/` could be linked into more than one executable). An
app is an executable — a composition root that wires a parser, the
engine, and a printer/publisher together. `apps/cli/main.cpp` wires
`CLIParser → MatchingEngine → ConsolePrinter` directly (no separate
`adapters/cli/`, since nothing else reuses a CLI parser yet);
`apps/replay/` will wire a CSV parser + the engine without touching the
CLI at all; `apps/benchmark/` wires the engine directly to Google
Benchmark. `adapters/tcp`, `adapters/fix`, `adapters/udp` exist because
those *are* reused — e.g. the FIX adapter and the TCP adapter both feed
the same exchange-server app in later phases.

**Dependency direction always points inward**: `apps/` and `adapters/`
depend on `interfaces/`, `interfaces/` types are implemented by
`engine/`, `engine/` depends on `orderbook/` and `core/`. Never the
reverse — `core/` and `orderbook/` know nothing about `engine/`;
`engine/` knows nothing about any adapter, app, socket, FIX tag, or
output format.

**Two channels out of the engine, not one:**
- `EngineAPI::submit`/`cancel` return an `EngineResponse` synchronously
  to *the caller* (accepted/rejected + aggregated fills) — this is what
  the CLI/TCP/FIX adapter reports back to whoever sent the order.
- The engine also pushes fine-grained events (`TradeExecuted`,
  `OrderAccepted`, `OrderCancelled`) through an injected `EventSink` —
  this is for *anyone else* who cares what happened (a UDP market-data
  adapter in Phase 6, a benchmark counter, a logger), independent of
  who originally submitted the order. Without this second channel,
  Phase 6 would need to reach back into the engine's internals to know
  a trade happened — exactly the coupling hexagonal architecture exists
  to avoid.

**Patterns used, deliberately kept to a short list** (Adapter, Ports &
Adapters, lightweight Observer for `EventSink`, plain constructor-based
dependency injection). Strategy/Factory are deferred until a phase
actually needs them (Phase 10 strategies, Phase 5+ adapter wiring). No
pattern gets added speculatively — a recruiter should see restraint,
not a design-pattern showcase.

Single-symbol, single-threaded is a deliberate choice, not a limitation
to apologize for: most real exchanges pin one instrument to one thread
rather than share a book across threads and pay for locking/cache
contention. Multi-symbol later means "N single-threaded engines," each
pinned to its own thread — not one engine made thread-safe internally.

## Repo layout (locked)

```
MiniExchange/
├── PLAN.md
├── CHARTER.md
├── .kiro/steering/      ← product.md, tech.md, structure.md, learning-doc.md
├── specs/
│   ├── phase-01-order-book/
│   ├── phase-02-benchmarking/
│   ├── phase-03-memory-pool/
│   ├── phase-04-lockfree-queue/
│   ├── phase-05-tcp-gateway/
│   ├── phase-06-udp-feed/
│   ├── phase-07-binary-protocol/
│   ├── phase-08-risk-engine/
│   ├── phase-09-fix-parser/
│   └── phase-10-strategy-sdk/
├── interfaces/          ← PORTS: EngineAPI (input), EventSink (output)
├── engine/              ← MatchingEngine (implements EngineAPI, uses orderbook + core, no I/O)
├── orderbook/           ← OrderBook data structures (price tree, intrusive list)
├── core/                ← Order.hpp, Trade.hpp, Events.hpp, Types.hpp, Price.hpp,
│                            Quantity.hpp, Side.hpp — domain primitives, no logic
├── adapters/            ← reusable translation libs (added as needed)
│   ├── tcp/             ← Phase 5
│   ├── udp/              ← Phase 6
│   ├── binary_protocol/ ← Phase 7
│   └── fix/             ← Phase 9
├── apps/                ← executables / composition roots
│   ├── cli/             ← Phase 1: main.cpp wires CLIParser → engine → ConsolePrinter
│   ├── replay/          ← later: CSV parser + engine, no CLI dependency
│   └── benchmark/       ← Phase 2+: engine + Google Benchmark
├── benchmarks/
├── tests/
├── scripts/
├── third_party/
└── docs/
```
(Only `apps/cli/` exists from Phase 1. `adapters/*` and the other
`apps/*` get created at the phase that introduces them — no empty
placeholder folders ahead of time.)

## Locked decisions (Phase 0)

| Area | Decision |
|---|---|
| Language | C++20 |
| OS | Linux only, Ubuntu 24.04 LTS |
| Build | CMake + Ninja |
| Testing | GoogleTest + Google Benchmark (libFuzzer later) |
| CI | GitHub Actions from day 1 (build, test, clang-tidy, cppcheck) |
| Price | Integer ticks, never floating point |
| Phase 1 order types | Limit, Market, Cancel only |
| Symbols | Single symbol for now |
| Matching | Strict price-time priority (FIFO) |
| Interface | CLI app only (`apps/cli/`); engine has zero I/O awareness |
| Order IDs | Client-supplied, engine validates uniqueness |
| Limits | Unbounded in Phase 1; memory pool arrives in Phase 3 |
| Price level queue | Custom intrusive doubly-linked list — **not** `std::list` |
| Cancel lookup | `unordered_map<OrderId, Order*>` → O(1) cancel |
| Price tree | `std::map<Price, PriceLevel>` baseline for Phase 1 (correctness-first); Phase 2/3 benchmark this against flat-array alternatives before deciding whether to replace it |

## Phase list & spec files

| Phase | Goal | Spec files (created at phase kickoff) |
|---|---|---|
| 1 | Limit order book + matching engine (single-threaded, single symbol) | requirements.md, design.md, tasks.md |
| 2 | Benchmark harness + baseline numbers | requirements.md, design.md, tasks.md, benchmarks.md |
| 3 | Memory pool, remove heap churn | requirements.md, design.md, tasks.md, benchmarks.md |
| 4 | Lock-free SPSC/MPSC queue between network + matching threads | requirements.md, design.md, tasks.md, benchmarks.md |
| 5 | TCP order gateway (epoll, nonblocking, TCP_NODELAY) | requirements.md, design.md, tasks.md |
| 6 | UDP market data feed (multicast simulation) | requirements.md, design.md, tasks.md |
| 7 | Binary wire protocol (vs JSON baseline) | requirements.md, design.md, tasks.md, benchmarks.md |
| 8 | Risk engine (position limits, price bands, fat-finger checks) | requirements.md, design.md, tasks.md |
| 9 | Minimal FIX parser (35=D/F/8) | requirements.md, design.md, tasks.md |
| 10 | Strategy SDK (market maker / momentum, for synthetic order flow) | requirements.md, design.md, tasks.md |

Each phase's `design.md` will also carry a **"Definition of Done"** section:
correctness tests passing, benchmark numbers recorded in `benchmarks/`,
and a short README write-up (the "5-minute recruiter story" the doc
mentioned).

## Status

**Phase 1: spec complete, ready for implementation.** `requirements.md`,
`design.md`, and `tasks.md` are all written and approved — see
`specs/phase-01-order-book/`. Execute `tasks.md` one task at a time
(per `.kiro/steering/structure.md`'s execution rule); `docs/adr/` and
`docs/LEARNING.md` don't exist yet in the repo because they're created
*by* that execution (Task 1 creates the skeleton including `docs/adr/`;
`LEARNING.md` accumulates from Task 2 onward), not pre-populated here.

**Phases 2–10: `requirements.md` only.** Each has an explicit "Open
Questions" section that must be resolved (in conversation, the same way
Phase 1's were) before that phase's `design.md` gets written — see
`.kiro/steering/structure.md`'s "Adding a new phase" section for the
exact process. Nothing beyond `requirements.md` should exist for these
phases yet; that's intentional, not a gap.
