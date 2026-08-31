# Structure

## Repo layout

This is the layout as it actually stands with all 10 phases complete
(originally a forward-looking plan; kept in sync as phases landed —
see the note below the tree for the two places reality diverged from
the original Phase-0 sketch):

```
MiniExchange/
├── PLAN.md                     ← master phase plan, single source of truth for ordering
├── .kiro/steering/               ← this folder: product.md, tech.md, structure.md, learning-doc.md
├── specs/
│   ├── phase-01-order-book/
│   │   ├── requirements.md
│   │   ├── design.md
│   │   └── tasks.md
│   ├── phase-02-benchmarking/
│   ├── phase-03-memory-pool/
│   ├── phase-04-lockfree-queue/
│   ├── phase-05-tcp-gateway/
│   ├── phase-06-udp-feed/
│   ├── phase-07-binary-protocol/
│   ├── phase-08-risk-engine/
│   ├── phase-09-fix-parser/
│   └── phase-10-strategy-sdk/
├── interfaces/                  ← PORTS: EngineAPI (input port), EventSink (output port)
├── engine/                     ← MatchingEngine: implements EngineAPI, uses orderbook + core, zero I/O
├── orderbook/                  ← OrderBook: price tree, intrusive list, PriceLevel, OrderPool (Phase 3)
├── core/                       ← Order.hpp, Trade.hpp, Events.hpp, Types.hpp (Price, Quantity, Side,
│                                   OrderId, ClientId, SymbolId, Sequence) — domain primitives, no logic
├── risk/                       ← RiskEngine: Decorator over EngineAPI (Phase 8) — fat-finger,
│                                   tick-size, price-band; STP itself lives in engine/ (ADR-007)
├── strategy/                   ← Strategy interface + MarketMakerStrategy/MomentumStrategy (Phase 10)
├── lockfree_queue/              ← SpscRingBuffer (Phase 4)
├── adapters/                   ← reusable translation libraries
│   ├── tcp/                    ← Phase 5
│   ├── text_protocol/          ← Phase 5 (extracted from apps/cli/)
│   ├── udp/                    ← Phase 6
│   ├── binary_protocol/        ← Phase 7
│   └── fix/                    ← Phase 9
├── apps/                       ← executables / composition roots
│   ├── cli/                    ← Phase 1: main.cpp wires CLIParser → engine → ConsolePrinter
│   ├── benchmark/               ← Phase 2+: engine + Google Benchmark
│   ├── exchange_server/         ← Phase 5+: TCP/UDP/binary/risk composition root (Linux only)
│   └── strategy_runner/         ← Phase 10: strategies + RiskEngine composition root
├── tools/                      ← workload_generator (Phase 2), protocol_benchmark (Phase 7)
├── benchmarks/results/
├── tests/
├── scripts/
└── docs/
```

Two deliberate divergences from the original Phase-0 sketch, corrected
here rather than left silently stale: `memory_pool/` was never created
as its own top-level directory — Phase 3's `OrderPool` turned out to
belong in `orderbook/` (it's the allocator behind `OrderBook`'s own
order lifetime, not a general-purpose facility used elsewhere), so it
lives at `orderbook/order_pool.hpp/.cpp`. `apps/replay/` and
`third_party/` were never created at all — no phase's `requirements.md`
ever called for a CSV-replay app (Phase 10 explicitly chose a new
`apps/strategy_runner/` over folding into a `replay/` app that doesn't
exist — see `specs/phase-10-strategy-sdk/requirements.md` §5), and
every dependency is `FetchContent`-vendored (GoogleTest, Google
Benchmark, nlohmann/json) rather than vendored into `third_party/`.

## Module responsibility boundaries

Dependency direction always points inward: `apps/*` and `adapters/*` →
`interfaces/` → `engine/` → `orderbook/` → `core/`. Never the reverse —
lower layers never know about, or depend on, anything above them.

- `core/`: pure data types and value objects — `Order`, `Trade`, `Side`,
  `Price`, `Quantity`, `OrderId`, `Sequence`, event payload types
  (`TradeExecuted`, `OrderAccepted`, `OrderCancelled`), `EngineResponse`,
  organized as focused headers (`Order.hpp`, `Trade.hpp`, `Events.hpp`,
  `Types.hpp`, `Price.hpp`, `Quantity.hpp`, `Side.hpp`) rather than one
  monolithic file. No logic beyond constructors/validation-free structs.
  No dependencies on any other MiniExchange module. (Deliberately not
  named `common/` — that name invites becoming a junk drawer over time;
  `core/` signals "domain primitives," nothing more.)
- `orderbook/`: owns the price tree + intrusive per-level order queues.
  Knows how to insert/remove/traverse price levels. Does **not** decide
  trading logic (that's `engine/`'s job) — it's a data structure, not a
  matching algorithm.
- `interfaces/`: the two ports. `EngineAPI` (input port) declares
  `submit`/`cancel`, returning `EngineResponse` synchronously to the
  caller. `EventSink` (output port, Observer-style) declares
  `on_trade`/`on_order_accepted`/`on_order_cancelled`, called by the
  engine for every state change regardless of who submitted the
  triggering order. Both `apps/*` and `adapters/*` depend on these
  abstractions, never on `engine/`'s concrete class directly.
- `engine/`: implements `EngineAPI`. Owns matching logic (price-time
  priority, fill generation, order acceptance/rejection). Depends on
  `orderbook/` and `core/` only. Zero I/O, zero threading, zero
  knowledge of CLI/TCP/FIX/UDP. Holds an injected `EventSink*`
  (constructor injection; a no-op default is fine when nobody's
  listening) and calls it alongside returning `EngineResponse`.
- `risk/` (Phase 8): `RiskEngine`, a Decorator over `EngineAPI` — see
  ADR-007. Owns exactly the three stateless, pre-matching config checks
  (fat-finger, tick-size, price-band); self-trade prevention is the
  deliberate exception that lives in `engine/` instead (matching-time
  concern, ADR-007's ordering argument). Depends on `interfaces/` and
  `core/` only — never `orderbook/` directly, since it only ever talks
  to its wrapped `EngineAPI`, never book internals.
- `strategy/` (Phase 10): `Strategy` interface plus
  `MarketMakerStrategy`/`MomentumStrategy` — ordinary `EngineAPI`
  clients with no privileged access to book state (NFR1, `strategy/README.md`),
  proving the port abstraction generalizes to algorithmic clients. Not
  profit-seeking; exists to generate realistic synthetic order flow.
- `adapters/*`: **reusable libraries**, not executables. Translate an
  external protocol/format into `EngineAPI` calls, translate
  `EngineResponse` back into that protocol/format, and optionally
  implement `EventSink`. An adapter exists only once something is
  actually shared across more than one app (e.g. `adapters/tcp/` and
  `adapters/fix/` both feed the exchange-server app in later phases).
  Adapters never contain matching logic.
- `apps/*`: **executables — composition roots.** Each app's `main.cpp`
  wires a parser (its own, or an `adapters/*` library), the engine, and
  an output/printer/publisher together. `apps/cli/` does not need a
  corresponding `adapters/cli/` — its `CLIParser`/`ConsolePrinter` live
  directly inside `apps/cli/` since nothing else reuses them (yet). If
  a second app ever needs to reuse the CLI's parsing logic, *that's*
  the signal to extract it into `adapters/`, not before.
- `specs/phase-NN-*/`: one folder per phase, each with
  `requirements.md` → `design.md` → `tasks.md`, approved in that order
  before code is written for that phase.

## Adding a new phase

1. Create `specs/phase-NN-<name>/requirements.md` (EARS-style, mirrors
   the format in `specs/phase-01-order-book/requirements.md`), including
   an explicit "Open Questions" section for anything genuinely
   ambiguous — don't force a premature decision just to avoid an open
   question.
2. **Before writing `design.md`, resolve every item in that phase's
   Open Questions section explicitly, in conversation** — the same
   iterative process used for Phase 1 (each question gets a decision
   and a stated reason, `requirements.md` gets updated to reflect it).
   Do not proceed to `design.md` with unresolved open questions still
   sitting in `requirements.md`.
3. Get explicit approval on the resolved `requirements.md` before
   writing `design.md`.
4. `design.md` covers concrete types/classes, the primary "why this
   over the alternative" tradeoff discussion, and a Definition of Done.
5. `tasks.md` is an ordered, checkable implementation list derived from
   `design.md`.
6. Only after all three are approved does code get written, scoped
   strictly to what `tasks.md` lists. **Execute one `tasks.md` item at
   a time, then stop for explicit review/approval before starting the
   next item** — do not run through the whole file unattended unless
   told to. This applies regardless of which agent/IDE is executing it.
7. After each implementation task, `docs/LEARNING.md` gets updated per
   `.kiro/steering/learning-doc.md` — bundled into the same task's
   review, not a separate checkpoint (see that file for what "bundled"
   means in practice).

## Naming conventions

- Types: `PascalCase` (`OrderBook`, `MatchingEngine`, `PriceLevel`).
- Functions/methods: `snake_case` (`add_order`, `cancel_order`).
- Enum values: `PascalCase` (`EngineResult::DuplicateOrderId`).
- File names mirror the primary type they define
  (`order_book.hpp`/`.cpp` for `OrderBook`).
- ADR files: `docs/adr/ADR-NNN-kebab-case-title.md` (e.g.
  `ADR-001-integer-prices.md`, `ADR-002-intrusive-linked-list.md`) —
  zero-padded three-digit number, so they sort correctly in a plain
  directory listing, plus a short descriptive slug so the filename
  alone tells you what the decision was without opening it.
