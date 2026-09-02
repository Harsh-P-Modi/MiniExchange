# Phase 11 / T9 (R9) — before/after syscall-count trace

This is the phase's designated acceptance evidence for R5 + R6: a
`perf trace -s` histogram of the `exchange_server` process under a
crossing-order load, captured on the **pre-Phase-11** build and the
**post-T6** build, same load shape and duration, comparing the counts
for `sendto`, `write`, and `sched_yield`.

## Measurement status: NOT captured in this environment

The development environment for this session is a Windows laptop
(msys2 ucrt64 toolchain) with **no Linux host and no WSL distribution
installed**. `apps/exchange_server`, the TCP adapter, and the binary
protocol codec are gated behind `if(UNIX AND NOT APPLE)` in
`CMakeLists.txt` (they use `epoll`, `eventfd`, `sendto`, `<endian.h>`),
so the server binary this trace targets **cannot be built or run here**,
and `perf` does not exist on this platform.

Rather than fabricate or hand-wave a syscall histogram, the numbers are
left explicitly pending a controlled Linux run — the same treatment
Phase 8 gave its T4 benchmark (`phase-08-order-size.md`), and consistent
with `phase-02-baseline.md` / `phase-05-tcp-roundtrip.md` recommending
Linux + `taskset` for any trustworthy figure.

What *was* verified in this environment: the mechanism changes
themselves, by unit test on every platform —
`QueuedEventSinkTest.EngineSubmitDoesNoSynchronousSinkWork` proves the
engine thread does only a ring push and no downstream I/O (T5), and
`WakeupCoalescerTest.BurstOfManyResponsesCollapsesToOneNotify` proves N
back-to-back responses produce 1 wakeup, not N (T6). R9 is the
end-to-end confirmation of those two on real hardware.

## Expected result (from the mechanism, to be confirmed by the trace)

Let an incoming order cross **k** resting orders (a k-fill order) and
produce one response.

| syscall | pre-Phase-11, per k-fill order | post-T6, per k-fill order | why |
|---|---|---|---|
| `sendto` (engine thread) | ~k (+1–2 for top-of-book / snapshot) | **0** | T5: `UdpFeedPublisher::on_trade`/`on_order_*` ran on the engine's stack and each called `sendto`. Now `QueuedEventSink` does a ring push; all `sendto` moved to the feed-publisher thread. |
| `sendto` (feed thread) | 0 (no such thread) | ~k (+1–2) | Same UDP traffic, issued from the drain thread instead. Total `sendto` across the process is ~unchanged; what changes is that **none are on the order→ack critical path**. |
| `write` (eventfd, engine thread) | 1 per response, i.e. 1 per order | **≈1 per inbound-queue drain cycle** | T6: the wakeup is coalesced to the moment the inbound queue reads empty. Under a burst of B pipelined orders the B writes collapse toward 1; under light load it stays 1 per order (no regression). |
| `sched_yield` | 1 per idle spin on each thread | ~unchanged | Not a target of R5/R6; recorded to show it didn't regress. Coalescing may *slightly* reduce engine-thread yields under load (fewer wakeup round-trips) — the trace will show by how much, if at all. |

Headline: an order crossing several resting orders previously drove
**~k `sendto` + 1 `write` on the engine thread before its response was
even queued**, against ~300 ns of actual matching work (research report
§1). After R5+R6 the engine thread issues **0 `sendto` and, under load,
a fraction of one `write`** per order. That gap closing is what R9
measures.

## Reproduction (on a Linux host)

Prerequisites: Ubuntu 24.04, `linux-tools-common` (`perf`), the repo
built for Linux (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`).

1. **Build the "before" server.** From a checkout at the commit
   immediately preceding this Phase 11 branch:
   ```
   git worktree add ../miniexchange-pre-p11 <pre-phase-11-commit>
   cmake -S ../miniexchange-pre-p11 -B ../miniexchange-pre-p11/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
   cmake --build ../miniexchange-pre-p11/build --target exchange_server
   ```

2. **Build the "after" server** from this branch:
   ```
   cmake --build build --target exchange_server
   ```

3. **Load generator.** Extend the existing serial client in
   `apps/benchmark/tcp_roundtrip_bench.cpp` (or a minimal standalone):
   open one TCP connection, then in a tight loop send framed `ADD`
   commands alternating buy/sell at the same price so roughly every
   other order is a 1-fill (produces trades → `sendto` on the feed) and
   top-of-book changes. Target a fixed count, e.g. 500,000 orders.
   Keep `TCP_NODELAY` on (matches the server).

4. **Trace each build** for the same fixed order count:
   ```
   taskset -c 2,3 ./build/exchange_server 9000 &            # or the pre-p11 binary
   SRV=$!
   perf trace -s -p $SRV -- sleep 0  &                       # attach
   # ... run the load generator to completion against :9000 ...
   kill -INT $SRV                                            # clean shutdown
   wait $SRV                                                 # perf prints the -s summary
   ```
   (Equivalently: `perf trace -s --summary -p $SRV` for the whole run,
   or `strace -f -c -p $SRV` if `perf` is unavailable — `strace -c` also
   gives a per-syscall count table.)

5. **Record** the `sendto`, `write`, and `sched_yield` rows from both
   summaries into a table here, plus: kernel version, CPU model, whether
   `taskset`/`numactl` was used, the UDP subscriber setup, and the exact
   order count and mix. Note per-thread counts if `perf trace` was run
   with `--per-thread` — the key figure is `sendto` **on the engine
   thread's TID**, which should be 0 on the "after" build.

## Acceptance

R9 is satisfied when the "after" trace shows, versus "before", (a)
`sendto` on the engine thread reduced to 0, and (b) engine-thread
`write` (eventfd) per filled order reduced under load — with the
reproduction steps and environment recorded alongside. Until that Linux
run happens, this file stands as the documented, un-omitted evidence
gap, per the Phase 8 T4 precedent.
