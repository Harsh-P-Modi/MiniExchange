# Tech

## Locked stack

| Area | Decision |
|---|---|
| Language | C++20 |
| OS | Linux only, Ubuntu 24.04 LTS. No Windows/macOS support, ever. |
| Build | CMake + Ninja |
| Testing | GoogleTest (correctness), Google Benchmark (perf). libFuzzer added once the order book is stable. |
| CI | GitHub Actions from day 1: build, run tests, run benchmarks (optional/non-blocking), clang-tidy, cppcheck |
| Debug/profiling tools | perf, perf record, taskset, numactl, hugepages, valgrind, gdb |

## Hard rules (apply to every phase unless a spec explicitly overrides)

- **No floating point in `core/`, `orderbook/`, or `engine/`.** Prices
  are integer ticks. Ever. No exceptions, no "just for display" leakage
  into core types.
- **No `std::list`.** Price-level order queues are custom intrusive
  doubly-linked lists (`prev`/`next` embedded in the `Order` struct
  itself, no separate node allocation).
- **The engine performs zero I/O.** No `printf`, `cout`, logging, file,
  or socket access anywhere in `engine/`, `orderbook/`, or `core/`.
  Presentation is exclusively an `apps/*` or `adapters/*` concern (CLI
  app in Phase 1, TCP/FIX adapters in later phases).
- **The engine returns structured results, never throws for expected
  business outcomes** (duplicate ID, unknown ID, invalid qty/price are
  results, not exceptions). Exceptions are reserved for programming
  errors (e.g. invariant violations), not client input.
- **Two output channels, not one.** `EngineAPI::submit`/`cancel` return
  `EngineResponse` synchronously to the immediate caller. Separately,
  the engine calls an injected `EventSink` (`on_trade`,
  `on_order_accepted`, `on_order_cancelled`) for every state change,
  regardless of who submitted the triggering order — this is what lets
  Phase 6's UDP feed (or a benchmark counter, or a logger) observe
  everything without the engine knowing they exist. `apps/*` and
  `adapters/*` depend on `interfaces/` (the ports), never on `engine/`'s
  concrete class.
- **Design patterns stay minimal and earned.** Adapter, Ports &
  Adapters, a lightweight Observer for `EventSink`, and plain
  constructor-based dependency injection are the full pattern list for
  now. Strategy and Factory are added only when a phase actually needs
  them (Phase 10, adapter wiring) — never speculatively. A recruiter
  should see restraint, not a pattern showcase.
- **Cancel and duplicate-ID detection are O(1) amortized** via
  `unordered_map<OrderId, Order*>` — never O(n) scans, regardless of
  book depth.
- **No wall-clock time inside the engine.** FIFO tiebreaking uses a
  monotonic `Sequence` counter assigned at insertion.
- **Client-supplied `OrderId`s**, validated for uniqueness by the
  engine — mirrors how FIX-based gateways work in practice.
- Don't reach for allocation/lock-free optimizations before they're
  earned by a benchmark. Phase 1 is correctness-first (`std::map` for
  the price tree is fine); Phase 2 measures before Phase 3 optimizes.

## Conventions

- Every mutating engine call returns an `EngineResponse` (status +
  fills + remaining quantity) — see `specs/phase-01-order-book/`.
- Phase order matters: don't add lock-free structures (Phase 4) or
  network I/O (Phase 5) into the engine core before the memory pool
  (Phase 3) is in place and benchmarked. Retrofitting is a smell here.
- Every phase's `design.md` includes a "why not X" section for the main
  data-structure/algorithm choice — recruiters read this, so don't skip
  it even when the answer feels obvious.

## Common commands (fill in once the CMake skeleton exists)

```
# Configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run benchmarks
./build/benchmarks/<target>
```
