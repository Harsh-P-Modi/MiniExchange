# Phase 11 / T8 (R8) — self-trade prevention: cost vs. book depth

Environment: same uncontrolled Windows laptop as every prior phase —
msys2 ucrt64 GCC + Ninja, RelWithDebInfo, no CPU pinning, turbo left on.
Absolute nanosecond figures on this box are noisy (see
`phase-02-baseline.md`); the number that matters here is the **ratio
across depths**, which is a shape, not an absolute latency.

## What changed

`MatchingEngine::would_self_cross` used to walk the crossable opposite
side of the book, scanning each price level's FIFO queue for a resting
order owned by the incoming client, until it found one or ran past the
incoming order's limit price. Cost scaled with how many levels/orders
that walk visited — O(book depth), and worst for a market order, which
has no price ceiling and would scan the entire opposite side.

T8 replaces the walk with a per-client index (`client_resting_`): for
each `ClientId`, an ordered `std::map<Price, count>` of the prices at
which that client currently has resting bids / asks, maintained
incrementally in `index_rest` / `index_unrest` next to every
`OrderBook::add_order` / `remove_order` the engine performs. The check
becomes: look up the incoming owner, read their best opposite-side
resting price (`begin()` / `rbegin()`, O(1)), compare to the incoming
limit. O(1), independent of book depth and of market-order sweep size.

## Measurement: the research report's depth-1/10/100/1000 shape

`tests/matching_engine_test.cpp` :: `StpDepthTest.CostDoesNotScaleWithDepth`.
For each depth D, build an ask book D levels deep owned by "client 2",
plus one resting ask owned by "client 1" at the far (deepest-to-cross)
price. Then time 200,000 STP-rejected `submit()` calls of a client-1 buy
priced to reach its own deep ask (rejected before any mutation, so the
loop is pure STP-check cost). 1,000 warm-up iterations discarded.

| Book depth | ns per STP-rejected submit | ratio vs depth 1 |
|---|---|---|
| 1     | 30.9 | 1.00x |
| 10    | 31.0 | 1.00x |
| 100   | 31.2 | 1.01x |
| 1000  | 32.2 | **1.04x** |

The research report measured the old walk at roughly **50x** between
depth 1 and depth 1000 for this shape (and it would be far worse for a
market order sweeping the whole side). Post-T8 the ratio is ~1.0x —
i.e. flat. The ~1 ns drift from depth 1 to depth 1000 is cache-footprint
noise (the deeper book touches more memory during *setup*), not
algorithmic: the check itself does the same one hash lookup + one
`std::map` extreme read + one integer compare regardless of D.

The test asserts `ratio(1000/1) < 8.0` — generous slack for this
unpinned box — which O(depth) at these depths could not possibly pass.

### Reproduction

```
cmake --build build --target matching_engine_test
./build/matching_engine_test --gtest_filter='StpDepthTest.*'
```

`StpDepthTest.CorrectAtLargeDepth` (same file) is the paired correctness
check: with 2,000 other-owned levels in front of the client's own deep
ask, the self-cross is still detected; a different client's identical
buy still trades; a below-own-ask buy still doesn't self-cross. All
Phase 8 STP behaviour tests pass unmodified — T8 changed cost, not
behaviour.

## Note on hot-path allocation (NFR3)

`client_resting_` is only touched when `stp_.enabled` — an engine with
STP off (the default, and what the benchmark harness uses) pays nothing.
When STP is on, `index_rest` allocates a `std::map` node only the first
time a given (client, side, price) triple appears; a market maker
re-quoting the same price ladder just increments an existing node's
count. This replaces an O(depth) scan (which did no allocation but was
the cost R8 exists to remove) with an O(1) check plus bounded,
mostly-amortized-away node churn keyed by each client's own quoting
breadth — not by book depth. See `docs/LEARNING.md` Phase 11 / T8.
