# Phase 11 / T7 (R7) — duplicate-ID tracking: retained-state comparison

Environment: same uncontrolled Windows laptop as every prior phase's
results — msys2 ucrt64 GCC + Ninja, RelWithDebInfo, no CPU pinning.
The claim here is about **retained state size**, which is exact and
platform-independent, not about latency (which this box cannot measure
below its ~100 ns noise floor — see `phase-02-baseline.md`).

## What changed

| | Before (Phases 1–10) | After (Phase 11 / T7) |
|---|---|---|
| Structure | `std::unordered_set<OrderId> ever_seen_ids_` | `std::unordered_map<ClientId, OrderId> last_accepted_id_` |
| Entry added | once **per accepted order**, forever | once **per distinct client**, updated in place thereafter |
| Size after N accepted orders from C clients | **N** entries | **C** entries |
| Per-check cost | 1 hash lookup (`.contains`) | 1 hash lookup (`.find`) + 1 integer compare |
| Semantic | global lifetime uniqueness | per-client monotonic uniqueness (requirements.md §7) |

The old set grew without bound: nothing ever removed an entry, so its
size tracked the lifetime count of accepted orders. On a venue that runs
for days this is a slow, permanent memory leak by design.

## Measurement: the research report's 200,000 add+cancel-cycle shape

Reproduced as a GoogleTest case,
`MatchingEngineWatermarkStress.StructureStaysBoundedOver200kCycles`
(`tests/matching_engine_test.cpp`). One client submits a
monotonically-increasing `OrderId` and immediately cancels it, 200,000
times; then two more clients do 1,000 cycles each.

`MatchingEngine::tracked_client_count()` (a new diagnostic accessor,
parallel to `book().order_count()`) is asserted after each phase:

| Point in the run | Accepted orders so far | `tracked_client_count()` | Old `ever_seen_ids_.size()` would be |
|---|---|---|---|
| After client 1's 200,000 cycles | 200,000 | **1** | 200,000 |
| After clients 2 & 3 add 1,000 cycles each | 202,000 | **3** | 202,000 |

The structure's size is bounded by **concurrent client count**, not by
cycle count — exactly the R7 goal. Wall time for the 200k-cycle loop on
this box: ~123 ms (add + cancel + all assertions), i.e. the per-cycle
cost is dominated by the `OrderBook` add/remove and the GoogleTest
`ASSERT`s, not by the watermark check.

### Reproduction

```
cmake --build build --target matching_engine_test
./build/matching_engine_test --gtest_filter='MatchingEngineWatermarkStress.*'
```

## Note on hot-path allocation (NFR3)

The old `ever_seen_ids_.insert(id)` allocated a hash-set node for **every
accepted order**. The new `last_accepted_id_[owner] = id` allocates a
map node only the **first time a given client is seen**; every
subsequent accepted order from that client is an in-place value
overwrite with zero allocation. Net effect: T7 *removes* per-order
hot-path allocation rather than adding any. See `docs/LEARNING.md`
Phase 11 / T7.
