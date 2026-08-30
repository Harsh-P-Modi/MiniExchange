# MiniExchange

A from-scratch, benchmarked, production-quality limit order book and matching
engine, built to demonstrate systems-engineering judgment for HFT / prop-trading
work. The point isn't that it "runs" — it's that every design decision is
justified, measured, and documented from first principles.

The core is a deterministic, single-threaded, allocation-conscious matching
engine. Everything else — networking, protocols, market-data feed, risk
checks — is an adapter or an app layered around it, never baked into the core.

## Project status

Built spec-first and phase-gated: each phase ships `requirements.md → design.md
→ tasks.md`, approved in order before code is written, with correctness tests
and (from Phase 2 on) benchmark numbers.

| Phase | Feature | Status |
|---|---|---|
| 1 | Limit order book + matching engine (single-symbol, single-threaded, price-time priority) | ✅ Complete |
| 2 | Benchmark harness + baseline numbers | ✅ Complete |
| 3 | Memory pool (remove per-order heap churn) | ✅ Complete |
| 4 | Lock-free SPSC queue (network thread → matching thread) | ✅ Complete |
| 5 | TCP order gateway (epoll, nonblocking, `TCP_NODELAY`) | ✅ Complete |
| 6 | UDP market-data feed (multicast simulation) | ✅ Complete |
| 7 | Binary wire protocol (vs JSON baseline) | ✅ Complete |
| 8 | Risk engine (fat-finger, tick-size, price-band, self-trade prevention) | ✅ Complete |
| 9 | Minimal FIX parser (35=D/F/8) | ⬜ Planned |
| 10 | Strategy SDK (synthetic order flow) | ⬜ Planned |

> **Build/CI target is Linux (Ubuntu 24.04).** The network-facing pieces
> (`apps/exchange_server`, the epoll TCP gateway, the UDP feed) are Linux-only
> by design and are built/tested there. The engine, order book, risk layer, and
> their tests are platform-neutral and also build under a Windows msys2/UCRT
> toolchain for development.

## Building

Requires: CMake 3.14+, Ninja, a C++20 compiler (GCC 10+, Clang 11+), Git.

```bash
# Configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run the benchmark harness (latency + throughput, prints and writes results)
./build/benchmark_harness
```

## What's inside

### The matching engine (Phases 1–3)

- **Strict price-time priority (FIFO).** Best price first; ties broken by a
  monotonic insertion `Sequence` counter, never by wall-clock time.
- **Integer-tick prices, no floating point** anywhere in `core/`, `orderbook/`,
  or `engine/`. This isn't cosmetic — it makes tick alignment an exact modulo
  and removes a whole class of rounding bugs.
- **Intrusive doubly-linked lists** for per-price-level order queues:
  `prev`/`next` live inside the `Order` struct itself, so a resting order
  carries its own queue membership — no separate node allocation, no
  `std::list`.
- **O(1) cancel and duplicate-ID detection** via `unordered_map<OrderId,
  Order*>` plus a back-pointer from each order to its `PriceLevel` — never an
  O(n) scan, regardless of book depth.
- **Pre-allocated order pool** (Phase 3): a fixed slab of `Order` slots with a
  free list, so steady-state order flow does zero heap allocation.
- **Two output channels.** `EngineAPI::submit`/`cancel` return an
  `EngineResponse` synchronously to the caller; separately, the engine calls an
  injected `EventSink` (`on_trade`/`on_order_accepted`/`on_order_cancelled`) for
  every state change — which is how the UDP feed, a benchmark counter, or a
  logger observe everything without the engine knowing they exist.
- **Structured results, not exceptions**, for expected business outcomes
  (duplicate ID, unknown ID, bad price/quantity are `EngineResult` codes).

### The gateway (Phases 4–7)

- **Two threads, two lock-free SPSC ring buffers.** An I/O thread runs an
  edge-triggered epoll loop (accept, read, parse, write); the matching thread
  spins on an inbound queue and pushes responses to an outbound queue. Orders
  are tagged with a `ClientId` so responses route back to the right connection.
- **Length-prefixed framing** that works for both a human-readable text protocol
  (for `netcat` debugging) and a compact binary protocol, selectable at startup.
- **UDP market-data feed** publishing top-of-book / trade events, driven purely
  off the engine's `EventSink` — the engine has no idea a feed exists.
- **Binary vs JSON protocol** benchmarked head-to-head (Phase 7).

### The risk engine (Phase 8)

A pre-trade risk layer that wraps the matching engine as a **Decorator** behind
the same `EngineAPI` port, so the gateway can't tell whether risk checks are
active:

- **Fat-finger** — reject orders whose quantity exceeds a configured ceiling.
- **Tick-size** — reject limit prices not aligned to the configured tick (exact
  integer modulo).
- **Price-band** — reject prices too far from a reference price, with the
  reference *seeded at construction* so the check is active from the first order
  on an empty book (no silent cold-start hole), then tracking the last trade
  price.
- **Self-trade prevention (STP)** — stop a client from trading against its own
  resting orders, with two policies: `RejectIncoming` (default) or
  `CancelResting`. STP lives *inside* the match loop rather than the decorator,
  because it's a matching-time concern and must not consume the order's ID or
  emit events before deciding — see the design doc for the full "why not the
  decorator" reasoning.
- Each rejection maps to a distinct `EngineResult`, and — because the decorator
  checks *before* delegating — a rejected order consumes no `OrderId` and has
  zero effect on book state.

## Try it

The single-process CLI (`apps/cli`) drives the engine directly:

```
> ADD 1 BUY 10000 50
ACCEPTED: no fills, remaining_qty=50
> ADD 2 SELL 10020 30
ACCEPTED: no fills, remaining_qty=30
> ADD 3 BUY 10020 20
ACCEPTED: FILL 20@10020 | FULLY FILLED
> PRINT_BOOK
=== ORDER BOOK ===
--- ASKS ---
  10020: qty=10
--- BIDS ---
  10000: qty=50
==================
> CANCEL 1
CANCELLED: order 1, remaining_qty=50
> QUIT
```

Commands: `ADD <id> BUY|SELL <price> <qty>`, `MARKET <id> BUY|SELL <qty>`,
`CANCEL <id>`, `PRINT_BOOK`, `QUIT`.

The networked exchange server (`apps/exchange_server`, Linux) wires the TCP
gateway → risk engine → matching engine → UDP feed, selectable between binary
and plaintext protocols with `--protocol=binary|plaintext`.

## Architecture

Hexagonal (ports & adapters). Dependency direction always points inward — lower
layers never know about upper layers.

```
   apps/cli    apps/exchange_server    apps/benchmark        ← executables /
       │              │                      │                  composition roots
       │        adapters/{tcp,udp,binary_protocol,text_protocol}
       │              │                      │
       └──────────────┴──────────┬───────────┘
                                  ▼
              ===== interfaces/ (PORTS) =====
                EngineAPI   (input:  submit / cancel)
                EventSink   (output: on_trade / on_order_accepted /
                                     on_order_cancelled — Observer)
              ================================
                                  ▼
                     risk/  (RiskEngine — Decorator over EngineAPI:
                             fat-finger, tick-size, price-band)
                                  ▼
                    engine/  (MatchingEngine — matching logic + STP,
                              zero I/O, single-threaded)
                                  ▼
                  orderbook/  (OrderBook: price tree + intrusive
                               per-level order queues; memory_pool)
                                  ▼
                       core/  (Order, Trade, Price, Quantity, Side,
                               OrderId, ClientId, Sequence, Events —
                               pure data, no logic)
```

Key principles:

- **No floating point** in `core/`/`engine/`/`orderbook/` — prices are integer
  ticks. (The risk layer's band *percentage* is a `double`, but it's converted
  to an integer tick bound before any comparison and never touches a stored
  price — see `docs/LEARNING.md`.)
- **No `std::list`** — intrusive lists only.
- **O(1) cancel** and duplicate detection.
- **Zero I/O in the engine** — all formatting, logging, sockets are an
  app/adapter concern.
- **The risk engine is a decorator, not a fork of the engine** — the matching
  engine stays a pure matching engine; risk is an opt-in layer wrapping it.
- **Design patterns kept to a short, earned list**: Adapter, Ports & Adapters,
  a lightweight Observer for `EventSink`, plain constructor DI, and Decorator
  (Phase 8). Nothing speculative.

## Documentation

- [`CHARTER.md`](CHARTER.md) — project goals, principles, invariants, coding
  standards
- [`docs/LEARNING.md`](docs/LEARNING.md) — long-form learning notes explaining
  every type and decision from first principles, phase by phase (the "why it's
  correct and what it costs" companion to the specs)
- [`docs/adr/`](docs/adr/) — Architecture Decision Records (terse: Context /
  Decision / Alternatives / Consequences)
- [`PLAN.md`](PLAN.md) — the 10-phase roadmap and locked decisions
- [`specs/`](specs/) — per-phase `requirements.md → design.md → tasks.md`
- [`benchmarks/results/`](benchmarks/results/) — recorded benchmark numbers per
  phase

## License

Educational / portfolio project — not for production use.
