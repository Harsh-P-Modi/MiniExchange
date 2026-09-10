# MiniExchange — Benchmark Results (controlled Linux run)

- **Date:** 2026-09-10
- **Host:** 13th Gen Intel(R) Core(TM) i5-13500H
- **OS:** Ubuntu 24.04.5 LTS
- **Kernel:** Linux 7.0.0-31-generic
- **Build:** RelWithDebInfo
- **Pinning:** `taskset -c 2,3` (codec benchmark: `-c 2`)
- **CPU governor:** performance
- **Turbo:** off
- **Commit:** 164e03b

## Summary

| Phase | Benchmark | Headline result | Status |
|---|---|---|---|
| 4 | SPSC ring vs mutex queue | 13.3× throughput (59.9M vs 4.5M ops/s); median latencies near-equal, mutex P99 ~1.7× worse | ✅ complete |
| 5 | TCP round-trip | ~34 μs median round trip, flat across ADD/CANCEL/1-fill; P99 ≈ 1.12× median | ✅ complete |
| 7 | Binary vs JSON codec | Binary 1.2–3.1 ns; JSON 870–1834 ns → **420–820×**; JSON does 11–36 heap allocs/op, binary 0; JSON payload 2.3–4.2× larger | ✅ complete |
| 8 | `Order` 64→72 B matching path | 64-byte side fully captured (controlled): ADD-100-fills 8,440 ns median, 9.57M ops/s mixed. 72-byte side needs a matched-pool run. | ⚠️ 64-byte done; 72-byte run pending |
| 11 / R9 | syscall-count trace | not captured — `perf trace` option combo and `strace -ff -c` both rejected on this kernel; known-good method (bare launch + `strace -f` attach) not yet run | ❌ pending |

## Phase 4 — SPSC ring buffer vs mutex queue

| Operation | Queue | Median (ns) | P99 (ns) |
|---|---|---|---|
| try_push | SpscRingBuffer | 28 | 37 |
| try_push | MutexQueue | 28 | 63 |
| try_pop | SpscRingBuffer | 22 | 26 |
| try_pop | MutexQueue | 26 | 43 |

| Queue | Two-thread throughput (ops/sec) |
|---|---|
| SpscRingBuffer | 59,895,400 |
| MutexQueue | 4,502,230 |
| **Speedup** | **13.30×** |

<details><summary>raw output</summary>

```text
Phase 4 Queue Benchmark
=======================

Generating workload (100000 ops for latency)...
Generating workload (2000000 ops for throughput)...
Measuring SpscRingBuffer push latency...
Measuring SpscRingBuffer pop latency...
Measuring MutexQueue push latency...
Measuring MutexQueue pop latency...
Measuring SpscRingBuffer throughput (5 reps, best-of)...
Measuring MutexQueue throughput (5 reps, best-of)...

--- Results ---
SpscRingBuffer push: median=28ns, P99=37ns
MutexQueue push: median=28ns, P99=63ns
SpscRingBuffer pop: median=22ns, P99=26ns
MutexQueue pop: median=26ns, P99=43ns
SpscRingBuffer throughput: 5.98954e+07 ops/sec
MutexQueue throughput: 4.50223e+06 ops/sec
Speedup: 13.3035x
```
</details>

## Phase 5 — TCP round-trip latency

| Metric | ADD (no match) | CANCEL | ADD (1 fill) |
|---|---|---|---|
| Iterations | 10,000 | 10,000 | 10,000 |
| Avg (μs) | 33.31 | 33.19 | 33.64 |
| Median (μs) | 34.46 | 34.47 | 35.25 |
| P99 (μs) | 38.70 | 38.68 | 38.72 |
| Max (μs) | 94.12 | 145.51 | 49.60 |

TCP overhead (round-trip median − Phase 2 engine-internal median):
~33.6 μs / ~34.2 μs / ~34.7 μs respectively. The ~34 μs fixed cost is
epoll wakeup + eventfd cross-thread wake + scheduler latency on
non-isolated, non-busy-polling cores — not the engine (300–900 ns) and
not the response size (one extra fill moves the median < 1 μs).

<details><summary>raw output</summary>

```text
=== TCP Round-Trip Latency Benchmark ===
Iterations: 10000

Server listening on port 43541

Results (10000 iterations each):
  ADD (no match, round-trip)      avg= 33307.3  median= 34457.5  P99= 38698.0  max= 94116.0 ns
  CANCEL (round-trip)             avg= 33194.9  median= 34468.5  P99= 38679.0  max=145511.0 ns
  ADD (1 fill, round-trip)        avg= 33639.6  median= 35254.0  P99= 38717.0  max= 49602.0 ns
```
</details>

## Phase 7 — binary vs JSON codec

Medians, `--benchmark_repetitions=10`, all rows CV < 1.1%.

| Message | Binary enc (ns) | JSON enc (ns) | enc ratio | Binary dec (ns) | JSON dec (ns) | dec ratio | Binary B | JSON B | size ratio | JSON allocs (enc/dec) |
|---|---|---|---|---|---|---|---|---|---|---|
| LimitOrderAdd | 2.70 | 1687 | 625× | 2.71 | 1752 | 647× | 34 | 78 | 2.29× | 36 / 14 |
| MarketOrderAdd | 1.72 | 1413 | 822× | 2.32 | 1441 | 621× | 26 | 64 | 2.46× | 31 / 13 |
| Cancel | 1.48 | 891 | 602× | 2.32 | 972 | 419× | 18 | 41 | 2.28× | 20 / 11 |
| Ack | 1.43 | 893 | 625× | 2.32 | 1026 | 442× | 18 | 46 | 2.56× | 20 / 12 |
| Reject | 1.20 | 871 | 726× | 1.93 | 1002 | 519× | 10 | 42 | 4.20× | 20 / 11 |
| TradeNotification | 3.09 | 1757 | 569× | 2.92 | 1834 | 628× | 42 | 99 | 2.36× | 36 / 15 |

Binary encode/decode do **0** heap allocations. JSON's cost is ~90% heap
traffic (20–36 small allocations per encode), ~10% ASCII int formatting /
field-name scanning.

<details><summary>raw output</summary>

```text
Run on (16 X 2373.95 MHz CPU s)
Load Average: 1.25, 1.89, 1.99
---------------------------------------------------------------------------------------------------
Benchmark                                         Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------
BM_BinaryEncode_LimitOrder_median              2.70 ns         2.70 ns           10 Bytes=34
BM_BinaryEncode_MarketOrder_median             1.72 ns         1.72 ns           10 Bytes=26
BM_BinaryEncode_Cancel_median                  1.48 ns         1.48 ns           10 Bytes=18
BM_BinaryEncode_Ack_median                     1.43 ns         1.43 ns           10 Bytes=18
BM_BinaryEncode_Reject_median                  1.20 ns         1.20 ns           10 Bytes=10
BM_BinaryEncode_TradeNotification_median       3.09 ns         3.09 ns           10 Bytes=42
BM_BinaryDecode_LimitOrder_median              2.71 ns         2.70 ns           10
BM_BinaryDecode_MarketOrder_median             2.32 ns         2.32 ns           10
BM_BinaryDecode_Cancel_median                  2.32 ns         2.32 ns           10
BM_BinaryDecode_Ack_median                     2.32 ns         2.32 ns           10
BM_BinaryDecode_Reject_median                  1.93 ns         1.93 ns           10
BM_BinaryDecode_TradeNotification_median       2.92 ns         2.91 ns           10
BM_JsonEncode_LimitOrder_median               1687 ns         1686 ns           10 Allocations=36 Bytes=78
BM_JsonEncode_MarketOrder_median              1413 ns         1413 ns           10 Allocations=31 Bytes=64
BM_JsonEncode_Cancel_median                    891 ns          891 ns           10 Allocations=20 Bytes=41
BM_JsonEncode_Ack_median                       893 ns          893 ns           10 Allocations=20 Bytes=46
BM_JsonEncode_Reject_median                    871 ns          871 ns           10 Allocations=20 Bytes=42
BM_JsonEncode_TradeNotification_median        1757 ns         1757 ns           10 Allocations=36 Bytes=99
BM_JsonDecode_LimitOrder_median              1753 ns         1752 ns           10 Allocations=14
BM_JsonDecode_MarketOrder_median             1442 ns         1441 ns           10 Allocations=13
BM_JsonDecode_Cancel_median                   973 ns          972 ns           10 Allocations=11
BM_JsonDecode_Ack_median                     1026 ns         1026 ns           10 Allocations=12
BM_JsonDecode_Reject_median                  1002 ns         1002 ns           10 Allocations=11
BM_JsonDecode_TradeNotification_median       1834 ns         1834 ns           10 Allocations=15
```
</details>

## Phase 8 — `Order` 64 → 72 bytes (matching path)

### 64-byte side — captured (controlled, commit 49d4a15, 4096-slot bench pool)

| Operation | Avg (ns) | Median (ns) | P99 (ns) | Max (ns) |
|---|---|---|---|---|
| ADD (no match) | 244.2 | 242.0 | 301.0 | 6,295 |
| ADD (1 fill) | 110.0 | 104.0 | 174.0 | 2,312 |
| ADD (10 fills) | 798.3 | 792.0 | 886.0 | 6,219 |
| ADD (100 fills) | 8,493.2 | 8,440.0 | 10,133.0 | 149,549 |
| CANCEL (front) | 71.8 | 69.0 | 102.0 | 608 |
| CANCEL (back) | 70.8 | 68.0 | 100.0 | 1,734 |
| Mixed throughput | | | | **9.57M orders/sec** |

First controlled capture of these — supersedes the Windows Phase 2 table
as the 64-byte reference.

### 72-byte side — still pending a matched run

The only 72-byte data (commit 164e03b) used the **1,000,000-slot pool per
iteration**: the untimed `new Order[1M]` + free-list stride evicts all
caches/TLB, so the timed op runs cold. It reported ADD (no match) 1,442 ns
median / ADD (1 fill) 710 ns (6–7× the 64-byte rows) — a cold-cache
artifact, not the struct-size cost — and stalled before ADD (10/100
fills). Close it with the harness from commit `5168fb0` (4096-slot pool)
on the same box; `bench_finish.sh` at repo root does this plus R9. See
`benchmarks/results/phase-08-order-size.md`.

## Phase 11 / R9 — syscall-count trace — PENDING

Not captured. Attempt 2 deadlocked (plaintext load vs binary-default
server). Attempt 3: `perf trace -s --per-thread` — option combo rejected
by this kernel's `perf`, printed usage. Attempt 4: `strace -ff -c` —
rejected (strace exited before `exec`). Known-good method not yet run:
bare-launch each server (`--protocol=plaintext`), sanity round-trip, then
`strace -f -e trace=sendto,write,… -p <pid>`, 20k alternating SELL/BUY
orders, `kill -INT`. Implemented in `bench_finish.sh`. See
`benchmarks/results/phase-11-syscall-trace.md`.

## Notes

- Cores were pinned with `taskset` but **not** isolated (`isolcpus` /
  `nohz_full`), so Phase 5's round-trip figure includes scheduler wakeup
  latency. Load average was ~1.9 during the Phase 7 run; per-row CV
  stayed < 1.1%.
- The `benchmark_harness` and `queue_benchmark` binaries rewrite
  `benchmarks/results/phase-03-pooled.md` and
  `phase-04-queue-comparison.md` in place; the run script backs them up
  and restores them, and the per-phase result files were updated by hand
  from this run's output.
