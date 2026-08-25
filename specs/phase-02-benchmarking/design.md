# Phase 2 — Design: Benchmark Harness + Baseline Numbers

Status: **DRAFT — awaiting your approval before tasks.md is written**

## 1. Overview

Two measurement mechanisms, per `requirements.md` §6: a custom
`LatencyRecorder` for single-operation latency (R1–R3), and standard
Google Benchmark for sustained throughput (R4). One shared
`tools/workload_generator/` library feeds both R4 and (later) Phase 10.
A fresh `MatchingEngine` is constructed per benchmark case, outside the
timed region — this is the "engine reset" `requirements.md` flagged as
possibly needing engine code changes; it turns out **no engine/
orderbook/core code changes are needed at all** (see §5), which is the
better outcome relative to that flagged possibility.

## 2. `tools/workload_generator/WorkloadGenerator.hpp`

```cpp
struct WorkloadConfig {
    uint64_t seed;
    Price mid_price;
    double price_stddev_log;     // log-normal sigma for |offset| from mid
    Quantity quantity_min;
    Quantity quantity_max;        // uniform within [min, max] for now —
                                   // simplest distribution that's still
                                   // configurable; revisit only if a
                                   // later phase's numbers demand more
                                   // realism than this provides
    double add_limit_ratio;       // mix ratios, must sum to 1.0
    double add_market_ratio;
    double cancel_ratio;
};

// One synthetic event the generator can produce. A separate type from
// NewOrder because CANCEL needs to reference a previously-generated,
// still-resting OrderId — something NewOrder alone can't express.
using WorkloadEvent = std::variant<LimitOrder, MarketOrder, CancelRequest>;

class WorkloadGenerator {
public:
    explicit WorkloadGenerator(WorkloadConfig config);

    // Generates `count` events. Internally tracks which OrderIds it
    // has generated and not yet cancelled/assumed-filled, so CANCEL
    // events reference plausible still-resting orders rather than
    // random IDs that would just bounce off UnknownOrderId. This
    // internal tracking is a simulated view, not queried from a real
    // engine — the generator doesn't know about actual fills, so it's
    // an approximation (see §6 for why that's acceptable here).
    std::vector<WorkloadEvent> generate(size_t count);

private:
    WorkloadConfig config_;
    std::mt19937_64 rng_;             // seeded from config_.seed — R5
    std::lognormal_distribution<double> price_offset_dist_;
    std::uniform_int_distribution<Quantity> quantity_dist_;
    std::vector<OrderId> assumed_resting_;   // candidates for CANCEL
    OrderId next_id_ = 1;
};
```

`CancelRequest` is a new, small type (`{ OrderId id; }`) — not part of
`core/` since it's a workload-generation concern, not a domain type the
engine itself needs (the engine's `EngineAPI::cancel(OrderId)` already
takes a bare `OrderId`; this struct just gives `WorkloadEvent`'s variant
a distinct alternative to `std::visit` on).

## 3. `apps/benchmark/LatencyRecorder.hpp` (app-local, not shared)

```cpp
class LatencyRecorder {
public:
    void record(std::chrono::nanoseconds duration);

    double avg_ns() const;
    double median_ns() const;
    double p99_ns() const;
    double max_ns() const;

private:
    std::vector<std::chrono::nanoseconds> samples_;
};
```

Percentiles computed by sorting `samples_` on read (acceptable here —
this runs once, after measurement, never in a timed region). Used like:

```cpp
LatencyRecorder recorder;
for (int i = 0; i < iterations; ++i) {
    MatchingEngine engine;              // fresh, untimed
    // ... untimed setup specific to the scenario (R1/R2/R3) ...
    auto start = std::chrono::steady_clock::now();
    engine.submit(order_under_test);    // the ONE thing being measured
    auto end = std::chrono::steady_clock::now();
    recorder.record(end - start);
}
```

## 4. Benchmark cases (R1–R3, `apps/benchmark/latency_bench.cpp`)

- `bench_add_no_match()` (R1): fresh engine, submit one non-crossing
  `LimitOrder`, measure only that call.
- `bench_add_with_match(size_t fill_count)` (R2): fresh engine,
  *untimed* setup inserts `fill_count` resting orders at crossing
  prices, then measure only the one incoming order that consumes all
  of them. Run for `fill_count ∈ {1, 10, 100}` per R2.
- `bench_cancel(Position pos)` (R3): fresh engine, *untimed* setup
  inserts several orders at one price level, measure only the
  `cancel()` call for either the front or back order (`pos` selects
  which) — confirms both are statistically indistinguishable, which is
  itself the point of this specific benchmark.

Each writes its `LatencyRecorder` stats into
`benchmarks/results/phase-02-baseline.md` (R6) via a small
`ResultsWriter` helper (app-local — formats a markdown table row per
benchmark case, appends to the results file).

## 5. Throughput case (R4, `apps/benchmark/throughput_bench.cpp`)

Standard Google Benchmark:

```cpp
static void BM_SustainedThroughput(benchmark::State& state) {
    WorkloadGenerator gen({...});
    auto events = gen.generate(100'000);   // pre-generated, untimed — NFR2

    for (auto _ : state) {
        MatchingEngine engine;              // fresh per repetition
        for (auto& event : events) {
            std::visit([&](auto&& e) { /* dispatch to submit/cancel */ },
                       event);
        }
    }
    state.SetItemsProcessed(state.iterations() * events.size());
}
BENCHMARK(BM_SustainedThroughput)->UseRealTime();
```

Google Benchmark's `items_processed`/`real_time` output converts
directly to orders/sec — no custom percentile logic needed here, since
R4 asks for throughput, not tail latency (R1–R3 already cover tail
latency for the operations that matter individually).

## 6. Why "fresh engine per iteration" resolves the reset question without touching `engine/`

`requirements.md`'s scope note flagged that Task work might need "a way
to reset engine state between benchmark iterations." Since Phase 1's
`MatchingEngine` has no memory pool yet (that's Phase 3) and its
constructor only initializes empty containers, constructing a brand new
`MatchingEngine` per iteration/repetition — outside the timed region —
is cheap enough not to distort the numbers, and requires zero new
methods on `engine/`/`orderbook/`/`core/`. This is worth explicitly
re-benchmarking once Phase 3's pool exists: a pooled engine's
constructor will do more work (pre-allocating the pool), and "construct
fresh per iteration" may need to become "reset a pooled engine back to
empty" at that point — flagging now so Phase 3 doesn't rediscover this
from scratch.

## 7. `benchmarks/results/phase-02-baseline.md` format

```markdown
# Phase 2 Baseline Results

Environment: [laptop, no CPU pinning | taskset -c N, turbo disabled]
Build: RelWithDebInfo, commit <hash>

## Single-operation latency (ns)

| Operation | Avg | Median | P99 | Max |
|---|---|---|---|---|
| ADD (no match) | ... | ... | ... | ... |
| ADD (1 fill) | ... | ... | ... | ... |
| ADD (10 fills) | ... | ... | ... | ... |
| ADD (100 fills) | ... | ... | ... | ... |
| CANCEL (front) | ... | ... | ... | ... |
| CANCEL (back) | ... | ... | ... | ... |

## Sustained throughput

| Workload | Orders/sec |
|---|---|
| Mixed (config: ...) | ... |
```

## 8. Judgment calls made here — flag if any should change

1. **Quantity distribution is plain uniform**, not log-normal like
   price offsets — I don't think quantity realism matters much for
   *this* phase's specific measurements (R1–R4 don't depend on
   quantity distribution shape the way they depend on price/mix ratio),
   and uniform is simpler. Worth revisiting only if Phase 10 finds it
   insufficient for strategy realism.
2. **`WorkloadGenerator`'s "assumed resting" tracking is approximate**
   — it doesn't know about actual fills (it's not wired to a real
   engine while generating), so a generated `CANCEL` could occasionally
   reference an ID the engine already fully filled, resulting in a
   harmless `UnknownOrderId` during the throughput run. This is
   acceptable for a throughput benchmark (an occasional rejected cancel
   doesn't meaningfully change ops/sec) but would **not** be acceptable
   if reused naively for Phase 10 strategies, where realistic
   behavior matters more than raw throughput — flagging so Phase 10
   doesn't assume this generator's cancel behavior is "correct" without
   re-checking.
3. **Fresh-engine-per-iteration** (§6) — my strong preference over
   adding a `reset()` method to `MatchingEngine`, since it achieves the
   same result with zero changes to code outside this phase's own
   `apps/benchmark`.

---

Once approved, `tasks.md` breaks this into ordered steps: `tools/
workload_generator/` first (Task order matters — Phase 10 will
eventually depend on it too, so it's worth getting right early),
then `LatencyRecorder`, then each benchmark case, then the results
writer and format, then the optional `scripts/run_benchmarks.sh`.
