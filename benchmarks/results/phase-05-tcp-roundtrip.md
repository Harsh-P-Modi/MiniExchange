# Phase 5 — TCP Round-Trip Latency Benchmark

## What is measured

End-to-end round-trip latency: the time from when a client sends a
framed order command over TCP until it receives the framed response
back on the same connection. This measures the full pipeline:

1. Client sends framed bytes over TCP
2. I/O thread: epoll wakeup → read → unframe → parse → push to inbound queue
3. Engine thread: pop from inbound → match → push response to outbound queue → eventfd write
4. I/O thread: eventfd wakeup → pop from outbound → render → frame → write to client socket
5. Client receives framed response bytes

## Methodology

- **Server:** full `apps/exchange_server` composition (TcpServer + MatchingEngine + SPSC queues + eventfd), started in-process by the benchmark harness
- **Client:** single-threaded TCP client on a blocking socket, sends one command, waits for one response, repeats (serial round-trips)
- **Measurement:** `std::chrono::steady_clock::now()` bracketing each send+recv pair
- **Warmup:** 100 round-trips before measurement begins (primes caches, page faults, thread scheduling)
- **Iterations:** 10,000 per operation type
- **Operations measured:**
  - `ADD (no match)` — non-crossing limit order, response is "ACCEPTED: no fills"
  - `CANCEL` — cancel a resting order, response is "ACCEPTED" or "REJECTED"
  - `ADD (1 fill)` — aggressive order crosses one resting order, response includes one fill
- **Client-side TCP_NODELAY:** enabled (matches server-side setting per R2)

## Environment requirements

Per requirement R7:
- Results MUST record whether `taskset`/`numactl` was used
- Stable comparisons SHOULD pin engine and I/O threads consistently
- Recommended: `taskset -c 0,1 ./build/tcp_roundtrip_bench` (pin both threads to cores 0,1)

## Comparison to Phase 2

Phase 2 measured engine-internal latency only (no network, no queues):

| Operation | Phase 2 median (ns) |
|---|---|
| ADD (no match) | 900 |
| ADD (1 fill) | 600 |
| CANCEL | 300 |

Phase 5's TCP round-trip adds on top:
- TCP send/recv syscall overhead (~1–3μs per pair on localhost)
- epoll wakeup latency (~1–2μs per edge-triggered notification)
- SPSC queue push/pop (sub-100ns per direction, per Phase 4 measurements)
- text_protocol parse + render (~100–200ns)
- eventfd write + read (~200–400ns)

Expected total: ~5–15μs round trip on localhost with CPU pinning.
The point of this benchmark is isolating "everything TCP adds on top"
of the engine-internal latency measured in Phase 2.

## Results

Collected on Linux (Ubuntu 24.04.5 LTS), Intel Core i5-13500H, kernel
7.0.0-31-generic, RelWithDebInfo, `taskset -c 2,3`, governor=performance,
turbo=off. Commit 164e03b. OS-assigned ephemeral port (43541 this run).

| Metric | ADD (no match) | CANCEL | ADD (1 fill) |
|---|---|---|---|
| Iterations | 10,000 | 10,000 | 10,000 |
| Avg (μs) | 33.31 | 33.19 | 33.64 |
| Median (μs) | 34.46 | 34.47 | 35.25 |
| P99 (μs) | 38.70 | 38.68 | 38.72 |
| Max (μs) | 94.12 | 145.51 | 49.60 |

### TCP overhead (round-trip median minus Phase 2 engine-internal median)

| Operation | Round-trip median | Engine-internal median | TCP overhead |
|---|---|---|---|
| ADD (no match) | 34.46 μs | 900 ns | ~33.6 μs |
| CANCEL | 34.47 μs | 300 ns | ~34.2 μs |
| ADD (1 fill) | 35.25 μs | 600 ns | ~34.7 μs |

### Reading the numbers

- **The response type barely matters.** ADD-no-match, CANCEL and
  ADD-1-fill land within ~0.8 μs of each other at the median. Confirmed:
  the engine work (300–900 ns) is negligible next to the round-trip
  fixed cost, and one extra fill in the response doesn't move it.
- **The fixed cost is ~34 μs, not the 5–15 μs this doc predicted.**
  That estimate assumed a busy-polling / co-scheduled I/O and engine
  thread. This run pins both threads to cores 2,3 with `taskset` but does
  **not** isolate those cores (`isolcpus`), busy-poll the sockets, or
  pin the eventfd wakeup path — so each round trip pays: two loopback TCP
  syscall pairs, an `epoll_wait` edge notification, an SPSC hop each
  direction, an `eventfd` write+read to wake the I/O thread, and at least
  one scheduler wakeup latency where the engine thread was idle on the
  inbound queue. On a non-isolated, non-polling setup ~30 μs of that is
  wakeup + loopback stack, which is the dominant term.
- **Tight distribution.** P99 is only ~1.12× the median for all three ops
  (38.7 vs ~34.5 μs), and the CANCEL max spike to 145 μs is a single
  scheduler outlier over 10,000 samples. The pipeline is consistent; it
  is just not busy-polling.
- **To get toward the single-digit-μs regime** would need `isolcpus` +
  `nohz_full` on the pinned cores, `SO_BUSY_POLL` or a userspace poll
  loop instead of `epoll_wait`, and replacing the `eventfd` cross-thread
  wake with a spin on the outbound queue. Those are deliberately out of
  scope here — this benchmark's job is to quantify what the current
  epoll+eventfd design costs, and the answer is ~34 μs round trip.

## How to run

```bash
# Build (Linux only — requires epoll + eventfd)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target tcp_roundtrip_bench

# Run with CPU pinning (recommended for stable measurements)
taskset -c 0,1 ./build/tcp_roundtrip_bench 0 10000

# Or without pinning (noisier, but still informative)
./build/tcp_roundtrip_bench 0 10000
```

## Notes

- Port 0 uses an OS-assigned ephemeral port (no conflict risk).
- The benchmark uses a single client — multi-client scalability is not
  the goal here; that's a separate concern for later phases.
- The serial send-wait-recv pattern means we measure latency, not
  throughput. Pipelining multiple requests would measure throughput
  (a different, valid benchmark for future phases).
- Build type must be RelWithDebInfo (or Release) for meaningful numbers —
  Debug builds include iterator checks and disable inlining.
