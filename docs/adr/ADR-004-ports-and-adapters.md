# ADR-004: Ports & Adapters (Hexagonal Architecture)

**Status:** Accepted  
**Date:** Phase 1 (documented Phase 5)

## Context

The matching engine needs to be:
- **Testable** without I/O (unit tests call `submit()`/`cancel()`
  directly, assert on `EngineResponse`).
- **Benchmarkable** without CLI overhead (Google Benchmark calls the
  same API the CLI uses).
- **Adaptable** to different frontends (Phase 1: CLI, Phase 5: TCP,
  Phase 7: binary protocol, Phase 9: FIX) without any modification to
  `engine/` or `orderbook/`.

This requires a clean separation between the domain logic (matching)
and its delivery mechanisms (console, network, file replay). The
question is which architectural pattern enforces that boundary.

## Decision

Two ports define the engine's entire external boundary:

1. **`EngineAPI`** (input port, `interfaces/engine_api.hpp`) — declares
   `submit()` and `cancel()`, returning `EngineResponse` synchronously.
   This is what callers depend on; they never see `MatchingEngine`
   directly.

2. **`EventSink`** (output port, `interfaces/event_sink.hpp`) — declares
   `on_trade()`, `on_order_accepted()`, `on_order_cancelled()`. The
   engine calls these for every state change. Observers (market data
   feed, test recorders, benchmark counters) implement this interface;
   the engine doesn't know who's listening.

Dependency direction is strictly inward:

```
apps/* and adapters/*  →  interfaces/  →  engine/  →  orderbook/  →  core/
```

Never the reverse. `engine/` depends on `interfaces/` only to
*implement* `EngineAPI`; it never includes headers from `apps/` or
`adapters/`. `apps/*` wire everything together (composition roots) but
are leaf nodes — nothing depends on them.

`EventSink` is injected into the engine via constructor injection
(a `NullEventSink` singleton serves as the no-op default when no
observer is wired up).

## Alternatives Considered

1. **Direct coupling (no interface)** — `apps/cli/main.cpp` includes
   `engine/matching_engine.hpp` directly and calls its methods.
   Rejected: the moment a second frontend appears (TCP, Phase 5), it
   would also need to include the concrete engine header. Every
   frontend would depend on every engine implementation detail. Swapping
   the engine (e.g. Phase 3's pool-backed allocator) would require
   touching every app. Violates dependency inversion.

2. **MVC (Model-View-Controller)** — engine is the model, CLI is the
   view+controller. Rejected: MVC's bidirectional dependencies between
   controller and model don't match the strict unidirectional flow
   needed here. Also doesn't naturally accommodate multiple output
   observers (EventSink) alongside a synchronous return channel
   (EngineResponse).

3. **Layered architecture (presentation → service → data)** — strict
   layers where each layer calls only the one below. Rejected:
   `EventSink` is a callback from a lower layer (engine) *up* to a
   higher layer (market data publisher), which violates strict layering
   without introducing an explicit port/interface. Once you add that
   interface, you've effectively arrived at Ports & Adapters anyway.

4. **Full CQRS (Command/Query Responsibility Segregation)** — separate
   command and query models, possibly with event sourcing. Rejected as
   massive overkill for a single-symbol, single-threaded engine. The
   project already has two output channels (synchronous response +
   broadcast events), but they read from the same internal state — no
   need for separate read/write models.

5. **Dependency injection framework (e.g. Boost.DI)** — automated
   wiring of dependencies. Rejected: plain constructor injection is
   trivial for the two-port surface this project has. A DI framework
   adds a compilation dependency, obscures the wiring site (which
   should be readable in `main.cpp`), and gains nothing when there are
   only two seams to inject.

## Consequences

- **Positive:** Zero I/O in `engine/` — all I/O is an app/adapter
  concern, enforced structurally (no I/O headers in `engine/`'s include
  path). Unit tests exercise the full matching logic with no filesystem,
  network, or console dependency. New frontends (TCP, FIX, binary) add
  an adapter and a composition root; the engine never changes. The two
  ports are small and stable (3 methods total on `EngineAPI`, 3 on
  `EventSink`), making the interface easy to implement and hard to
  break accidentally.
- **Negative:** Adds one level of indirection (virtual dispatch
  through `EngineAPI` and `EventSink`). Cost is negligible — one
  vtable lookup per `submit()`/`cancel()` call, and the engine is
  called thousands of times per second, not billions. If it ever
  matters (it won't), the ports can be templated for compile-time
  dispatch without changing the architecture.
- **Enforced by:** CMake target dependencies prevent `engine/` from
  linking against `apps/` or `adapters/`. `interfaces/` is a
  header-only library with no implementation files — it physically
  cannot contain logic, only contracts. Code review / CI linting
  catches any `#include` from engine/ reaching into apps/ or adapters/.
