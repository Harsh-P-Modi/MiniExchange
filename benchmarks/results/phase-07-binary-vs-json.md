# Phase 7 — Binary vs JSON Protocol Benchmark

## What is measured

Codec-level encode and decode latency for the binary fixed-layout
protocol vs. the nlohmann/json-based JSON representation, for each of
the six message types used by the exchange server. This is a
**codec-only** comparison — it does not include TCP round-trip, queue
transit, or engine processing time (those are measured separately in
Phase 5's TCP round-trip benchmark).

Additionally: payload size in bytes and heap allocation count per
operation, to attribute *where* any latency difference comes from
(allocation overhead vs. parsing/formatting CPU time).

## Methodology

- **Framework:** Google Benchmark (`benchmark::benchmark` v1.9.1)
- **Repetitions:** `--benchmark_repetitions=10` for statistical confidence
- **Statistics:** min/median/mean/stddev reported by Google Benchmark
- **Binary codec:** encodes into a pre-allocated stack buffer (`std::array<std::byte, 42>`);
  decodes from a pre-encoded span. Zero heap allocations (verified by task 8's
  instrumented test).
- **JSON codec:** `nlohmann::json j = msg; j.dump()` for encode;
  `nlohmann::json::parse(s).get<Msg>()` for decode. Heap allocations are
  inherent to nlohmann/json's internal representation and string operations.
- **Allocation counting:** Global `operator new` override with thread-local
  counter, enabled via RAII guard during each JSON iteration. Binary iterations
  are expected to show 0 allocations (confirmed by task 8).
- **DoNotOptimize:** Applied to prevent dead-code elimination of results.

## Environment requirements

- Linux x86_64 (the project's target platform)
- Recommend: `taskset -c 0 ./build/protocol_benchmark` to pin to a single core
  for stable numbers
- CPU frequency governor set to `performance` for reproducibility
- Results below are placeholders — fill after running on the target machine

## Results

### Encode Latency (ns/op)

| Message Type | Binary | JSON | Ratio (JSON/Binary) |
|---|---|---|---|
| LimitOrderAdd | _TBD_ | _TBD_ | _TBD_ |
| MarketOrderAdd | _TBD_ | _TBD_ | _TBD_ |
| Cancel | _TBD_ | _TBD_ | _TBD_ |
| Ack | _TBD_ | _TBD_ | _TBD_ |
| Reject | _TBD_ | _TBD_ | _TBD_ |
| TradeNotification | _TBD_ | _TBD_ | _TBD_ |

### Decode Latency (ns/op)

| Message Type | Binary | JSON | Ratio (JSON/Binary) |
|---|---|---|---|
| LimitOrderAdd | _TBD_ | _TBD_ | _TBD_ |
| MarketOrderAdd | _TBD_ | _TBD_ | _TBD_ |
| Cancel | _TBD_ | _TBD_ | _TBD_ |
| Ack | _TBD_ | _TBD_ | _TBD_ |
| Reject | _TBD_ | _TBD_ | _TBD_ |
| TradeNotification | _TBD_ | _TBD_ | _TBD_ |

### Payload Size (bytes)

| Message Type | Binary | JSON | Ratio (JSON/Binary) |
|---|---|---|---|
| LimitOrderAdd | 34 | _TBD_ | _TBD_ |
| MarketOrderAdd | 26 | _TBD_ | _TBD_ |
| Cancel | 18 | _TBD_ | _TBD_ |
| Ack | 18 | _TBD_ | _TBD_ |
| Reject | 10 | _TBD_ | _TBD_ |
| TradeNotification | 42 | _TBD_ | _TBD_ |

### Heap Allocations per Operation

| Message Type | Binary Encode | Binary Decode | JSON Encode | JSON Decode |
|---|---|---|---|---|
| LimitOrderAdd | 0 | 0 | _TBD_ | _TBD_ |
| MarketOrderAdd | 0 | 0 | _TBD_ | _TBD_ |
| Cancel | 0 | 0 | _TBD_ | _TBD_ |
| Ack | 0 | 0 | _TBD_ | _TBD_ |
| Reject | 0 | 0 | _TBD_ | _TBD_ |
| TradeNotification | 0 | 0 | _TBD_ | _TBD_ |

## Interpretation

_To be filled after running the benchmark. The analysis should address:_

1. **Latency attribution:** Is JSON's disadvantage primarily from heap
   allocation (each `nlohmann::json` object allocates for its internal
   map/string storage) or from the parsing/formatting CPU work itself
   (scanning field names, formatting integers as ASCII decimal, etc.)?
   The allocation count column provides the evidence — if JSON allocates
   N times per encode and binary allocates 0, and the latency ratio
   roughly tracks the allocation count, then allocation dominates. If
   the ratio is much larger than the allocation count alone would
   explain, then CPU formatting time is also significant.

2. **Payload size:** Binary's fixed-layout encoding eliminates field
   names, braces, colons, and quotes that JSON carries. The size ratio
   is expected to be 3–5× (JSON carries ~60–100 bytes of field name +
   formatting overhead per message vs. binary's 10–42 bytes of pure
   data). This matters for network bandwidth and cache pressure in
   high-throughput scenarios.

3. **What this comparison does NOT tell you:** This is a codec
   microbenchmark in isolation. The actual end-to-end latency benefit
   of switching from text to binary in the TCP gateway includes the
   codec difference *plus* reduced payload size (fewer bytes through
   the TCP stack) *minus* the fact that the gateway's bottleneck may
   be elsewhere (epoll wakeup, queue transit, engine matching). The
   Phase 5 TCP round-trip benchmark provides the system-level baseline;
   this Phase 7 benchmark isolates just the serialization layer to
   show where the overhead lives.

4. **Honest caveats:**
   - nlohmann/json is a convenience/correctness library, not optimized
     for latency. A production JSON parser (simdjson, rapidjson) would
     close the gap significantly on the decode side.
   - The binary codec's zero-allocation advantage is structural (fixed
     sizes, caller-provided buffers) — it would persist even against a
     faster JSON library, since JSON's variable-length nature
     fundamentally requires dynamic allocation somewhere.
   - Both codecs are single-threaded measurements — no contention effects.

## How to reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
taskset -c 0 ./build/protocol_benchmark --benchmark_repetitions=10 \
    --benchmark_out=benchmarks/results/phase-07-raw.json \
    --benchmark_out_format=json
```
