# Structure

## Repo layout

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
├── orderbook/                  ← OrderBook: price tree, intrusive list, PriceLevel
├── core/                       ← Order.hpp, Trade.hpp, Events.hpp, Types.hpp, Price.hpp,
│                                   Quantity.hpp, Side.hpp — domain primitives, no logic
├── adapters/                   ← reusable translation libraries (created when needed)
│   ├── tcp/                    ← Phase 5
│   ├── udp/                    ← Phase 6
│   ├── binary_protocol/        ← Phase 7
│   └── fix/                    ← Phase 9
├── apps/                       ← executables / composition roots
│   ├── cli/                    ← Phase 1: main.cpp wires CLIParser → engine → ConsolePrinter
│   ├── replay/                 ← later: CSV parser + engine, no CLI dependency
│   └── benchmark/               ← Phase 2+: engine + Google Benchmark
├── memory_pool/                ← added Phase 3
├── lockfree_queue/              ← added Phase 4
├── benchmarks/
├── tests/
├── scripts/
├── third_party/
└── docs/
```

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
