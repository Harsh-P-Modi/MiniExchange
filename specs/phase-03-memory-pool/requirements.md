# Phase 3 — Requirements: Memory Pool

Status: **DRAFT — spec-only pass, design.md deferred until this phase starts**

## 1. Scope

Replace `OrderBook`'s `unordered_map<OrderId, unique_ptr<Order>>`
ownership (Phase 1) with a fixed-capacity, pre-allocated pool, removing
per-order heap allocation from the hot path entirely. Benchmark against
Phase 2's baseline to quantify the improvement — per the Charter's
benchmark philosophy, this phase doesn't happen without Phase 2's
numbers to compare against.

Out of scope: pooling for the lifetime-unique `ever_seen_ids_` set
(`requirements.md` §2.1 of Phase 1) — that set's growth is a *different*
problem (it never shrinks, by design, since IDs are never reused) and
is explicitly not addressed by this phase **or any other phase in this
project's scope** (see Phase 1 `requirements.md` §2.1, updated) — it's
an accepted permanent limitation of this simulator, not a task waiting
to be picked up later.

## 2. Functional Requirements (EARS)

- R1: THE ENGINE SHALL pre-allocate a fixed-capacity pool of `Order`
  slots at startup (default capacity TBD — see Open Questions).
- R2: WHEN a new order is accepted, THE ENGINE SHALL acquire a slot
  from the pool in O(1) (free-list pop), not via `new`/heap allocation.
- R3: WHEN an order is fully filled or cancelled, THE ENGINE SHALL
  return its slot to the pool's free list in O(1).
- R4: WHEN the pool has no free slots available and a new order is
  submitted, THE ENGINE SHALL reject it with a new `EngineResult`
  value (e.g. `PoolExhausted`) rather than falling back to heap
  allocation, crashing, or blocking.
- R5: `Order::prev`/`next`/`level` pointers (Phase 1 design) SHALL
  continue to work unchanged — the pool must not reallocate/move
  slots once allocated (a fixed-capacity array satisfies this
  trivially; anything that could move memory would invalidate every
  intrusive pointer in the system and must be rejected as a design).
- R6: Benchmark comparison against Phase 2's baseline SHALL be recorded
  (allocation latency specifically, plus overall throughput/latency
  deltas), in the same format as Phase 2's results file.

## 3. Non-Functional Requirements

- NFR1: Zero calls to `new`/`delete`/`malloc`/`free` for `Order`
  lifetime management after the initial pool allocation at startup.
- NFR2: Pool acquire/release remain O(1) regardless of pool
  fill-level (no linear scan for a free slot).

## 4. Definition of Done

- All Phase 1 tests still pass unchanged (pooling is an internal
  ownership swap, invisible to `orderbook/`'s and `engine/`'s logic
  per the Phase 1 design's explicit goal for this swap).
- New tests cover pool exhaustion (`PoolExhausted` returned correctly,
  no crash, existing resting orders unaffected).
- Benchmark numbers recorded and compared against Phase 2 baseline.

## 5. Open Questions (resolve before design.md for this phase)

1. **Default pool capacity** — the original planning doc mentioned
   1,000,000 as an example; is that the right default, or should it be
   configurable at `MatchingEngine` construction time (recommended —
   trivial to add, and useful for the pool-exhaustion tests)?
2. **Free-list implementation** — intrusive free list (reuse the
   now-unused slot's own memory to store a "next free" pointer, zero
   extra memory) vs. a separate `std::vector<uint32_t>` of free
   indices? The intrusive approach is more idiomatic for this kind of
   pool and costs nothing extra, but worth confirming you want that
   over the simpler (if slightly wasteful) vector-of-indices approach.
3. Should the pool be a generic, reusable `Pool<T>` template (usable
   later for `PriceLevel` nodes or elsewhere if needed), or a
   `Order`-specific pool? A generic template is a small amount of extra
   work now that could pay off, but risks over-engineering for a
   single current use case.
