# MiniExchange

A from-scratch, production-quality limit order book and matching engine built to demonstrate systems-engineering judgment to HFT/prop-trading recruiters. Every design decision is justified, benchmarked (Phase 2+), and documented.

## Project Status

**Phase 1 complete** — single-symbol, single-threaded matching engine with strict price-time priority, limit orders, market orders, and cancel. Phase 2 (benchmarking) pending.

## Building

Requires: CMake 3.14+, Ninja, a C++20 compiler (GCC 10+, Clang 11+), Git.

```bash
# Configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

## Usage

Run the CLI application:

```bash
./build/mini_exchange
```

Example session:

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

Commands:
- `ADD <id> BUY|SELL <price> <qty>` — submit a limit order
- `MARKET <id> BUY|SELL <qty>` — submit a market order
- `CANCEL <id>` — cancel a resting order
- `PRINT_BOOK` — display current book depth
- `QUIT` — exit

## Architecture

```
                    ┌─────────────┐
                    │  apps/cli/  │  (composition root — wires parser → engine → printer)
                    └──────┬──────┘
                           │ depends on
                    ┌──────▼──────┐
                    │ interfaces/ │  (EngineAPI input port, EventSink output port)
                    └──────┬──────┘
                           │ implemented by
                    ┌──────▼──────┐
                    │   engine/   │  (MatchingEngine — matching logic, zero I/O)
                    └──────┬──────┘
                           │ uses
                    ┌──────▼──────┐
                    │  orderbook/ │  (price tree + intrusive per-level order queues)
                    └──────┬──────┘
                           │ uses
                    ┌──────▼──────┐
                    │    core/    │  (Order, Trade, Price, Quantity, Side — pure data)
                    └─────────────┘
```

Dependency direction is strictly inward. Lower layers never know about upper layers.

Key design principles:
- **No floating point** in core/engine/orderbook — prices are integer ticks
- **Intrusive doubly-linked lists** for per-level order queues (no `std::list`)
- **O(1) cancel** via `unordered_map<OrderId, Order*>` + back-pointer to PriceLevel
- **Two output channels**: `EngineResponse` (synchronous, per-caller) + `EventSink` (broadcast observer)
- **Zero I/O in the engine** — all formatting/logging is an apps/adapters concern

## Documentation

- [`CHARTER.md`](CHARTER.md) — project goals, principles, invariants, and coding standards
- [`docs/LEARNING.md`](docs/LEARNING.md) — detailed learning notes explaining every design decision from first principles
- [`docs/adr/`](docs/adr/) — Architecture Decision Records (terse reference: Context / Decision / Alternatives / Consequences)
- [`PLAN.md`](PLAN.md) — 10-phase roadmap
- [`specs/phase-01-order-book/`](specs/phase-01-order-book/) — Phase 1 requirements, design, and task breakdown

## License

Educational/portfolio project — not for production use.
