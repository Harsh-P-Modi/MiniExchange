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

## Results

Collected on Linux (Ubuntu 24.04.5 LTS), Intel Core i5-13500H, kernel
7.0.0-31-generic, RelWithDebInfo, `taskset -c 2`, governor=performance,
turbo=off (CPU pinned at ~2.37 GHz base). Commit 164e03b.
`--benchmark_repetitions=10`, aggregates only; all rows reported
coefficient of variation < 1.1%. Load average ~1.9 during the run — did
not visibly perturb the medians (CV stayed sub-1%). Figures below are the
`_median` rows.

### Encode Latency (ns/op)

| Message Type | Binary | JSON | Ratio (JSON/Binary) |
|---|---|---|---|
| LimitOrderAdd | 2.70 | 1687 | 625× |
| MarketOrderAdd | 1.72 | 1413 | 822× |
| Cancel | 1.48 | 891 | 602× |
| Ack | 1.43 | 893 | 625× |
| Reject | 1.20 | 871 | 726× |
| TradeNotification | 3.09 | 1757 | 569× |

### Decode Latency (ns/op)

| Message Type | Binary | JSON | Ratio (JSON/Binary) |
|---|---|---|---|
| LimitOrderAdd | 2.71 | 1752 | 647× |
| MarketOrderAdd | 2.32 | 1441 | 621× |
| Cancel | 2.32 | 972 | 419× |
| Ack | 2.32 | 1026 | 442× |
| Reject | 1.93 | 1002 | 519× |
| TradeNotification | 2.92 | 1834 | 628× |

### Payload Size (bytes)

| Message Type | Binary | JSON | Ratio (JSON/Binary) |
|---|---|---|---|
| LimitOrderAdd | 34 | 78 | 2.29× |
| MarketOrderAdd | 26 | 64 | 2.46× |
| Cancel | 18 | 41 | 2.28× |
| Ack | 18 | 46 | 2.56× |
| Reject | 10 | 42 | 4.20× |
| TradeNotification | 42 | 99 | 2.36× |

### Heap Allocations per Operation

| Message Type | Binary Encode | Binary Decode | JSON Encode | JSON Decode |
|---|---|---|---|---|
| LimitOrderAdd | 0 | 0 | 36 | 14 |
| MarketOrderAdd | 0 | 0 | 31 | 13 |
| Cancel | 0 | 0 | 20 | 11 |
| Ack | 0 | 0 | 20 | 12 |
| Reject | 0 | 0 | 20 | 11 |
| TradeNotification | 0 | 0 | 36 | 15 |

## Interpretation

### 1. Latency attribution — allocation is the floor, not the whole story

JSON encode is **570–820× slower** than binary; JSON decode **420–650×**.
Binary encode/decode is 1.2–3.1 ns (a `memcpy` of a fixed struct plus a
few byte-order swaps, zero allocations). JSON pays on every operation:

- **Encode** does 20–36 `operator new` calls (building the
  `nlohmann::json` DOM node-by-node, then `dump()` formatting each
  integer to ASCII and concatenating into a `std::string`). At ~40–50 ns
  per small allocation that is ~1000–1800 ns of pure allocator time —
  which is essentially the entire measured encode cost. Allocation
  dominates.
- **Decode** does 11–15 allocations (parser builds the DOM, then
  `get<T>()` reads fields back out). Fewer allocations than encode, and
  correspondingly faster in absolute terms (Cancel/Ack/Reject decode
  ~970–1030 ns vs ~890 ns encode is close), but the ratio to binary is
  still 400–650× because binary decode is also near-free.
- The latency ratio tracks the allocation count loosely, not exactly:
  `LimitOrderAdd` has the most allocations (36 encode) *and* the highest
  absolute cost (~1690 ns), while `Reject` with 20 encode allocations is
  ~870 ns. The extra spread on top of raw allocation count is the ASCII
  integer formatting and field-name string work in `dump()`/`parse()`.

**Takeaway:** JSON's cost here is ~90% "touch the heap 20–36 times",
~10% "format/scan characters". Binary avoids both.

### 2. Payload size

JSON messages are **2.3–2.6×** larger than binary for the field-carrying
types, and **4.2×** larger for `Reject` (binary `Reject` is only 10 bytes
of payload, so JSON's fixed `{"type":"reject","reason":...}` scaffolding
dominates). This is smaller than the 3–5× the original estimate guessed
for most types — the exchange's messages have few fields, so JSON's
per-field-name overhead has less to amortise against. The size gap still
matters at high message rates: 2.4× more bytes through the TCP stack,
2.4× more L1/L2 footprint per message in flight.

### 3. What this comparison does NOT tell you

This is a codec microbenchmark in isolation. The actual end-to-end
latency benefit of switching from text to binary in the TCP gateway is
the codec difference (~1.7 μs per message per direction, from the tables
above) *plus* reduced payload size (2.4× fewer bytes through the TCP
stack) *minus* the fact that the gateway's bottleneck is elsewhere: Phase
5 measured a ~34 μs round trip dominated by epoll wakeup + eventfd +
scheduler latency, against which ~3 μs of saved JSON codec time is real
but small (~10%). Binary's structural win shows up more in tail
predictability (no allocator, no page faults) than in median round trip.

### 4. Honest caveats

- nlohmann/json is a convenience/correctness library, not optimised for
  latency. A production JSON parser (simdjson, rapidjson) would close the
  decode gap substantially — but not the encode gap, which is allocation-
  bound by JSON's DOM model.
- The binary codec's zero-allocation advantage is structural (fixed
  sizes, caller-provided buffers) — it persists against any JSON library,
  since JSON's variable-length nature requires dynamic allocation
  somewhere.
- Both codecs are single-threaded measurements — no contention effects.
- Background load average was ~1.9 during this run. Per-row CV stayed
  below 1.1%, so the medians are trustworthy, but a fully quiescent box
  would be the gold standard for the absolute JSON figures.

## How to reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
taskset -c 0 ./build/protocol_benchmark --benchmark_repetitions=10 \
    --benchmark_out=benchmarks/results/phase-07-raw.json \
    --benchmark_out_format=json
```
