# Phase 11 — Tasks: Iteration

Status: **IMPLEMENTED** — T1–T11 done on this branch. T9 (perf trace) is
documented for a Linux run; see benchmarks/results/phase-11-syscall-trace.md.

`requirements.md` and `design.md` are resolved and approved. Tasks are
ordered so each is independently testable and gated on the ones before
it where a real dependency exists (see design.md §10 for the full
dependency reasoning — most tasks in this phase are *not* dependent on
each other). Execute one task at a time and stop for review before the
next, per `.kiro/steering/structure.md` step 6. Each implementation task
bundles its `docs/LEARNING.md` addition into the same review, per
`.kiro/steering/learning-doc.md`.

---

## Correctness fixes (independent of each other and of everything below)

- [x] **T1** — R1: explicit rejection on inbound queue full
      (design.md §1). In `apps/exchange_server/main.cpp`'s frame
      handler, check `inbound.try_push`'s return value; on `false`,
      render and send a rejection via the existing `render_error_fn` /
      `frame_message` / `server.send_to_client` path already used for
      `ParseError`.
      Tests: fill the inbound queue to capacity (small test-only
      capacity or direct queue manipulation), submit one more order,
      assert the client receives an explicit rejection frame rather
      than silence, and assert no `TaggedCommand` for that order
      reaches the engine.

- [x] **T2** — R2: `OrderId` on `EngineResponse`
      (design.md §2). Add `OrderId order_id` to `EngineResponse`
      (`core/Events.hpp`). Set it at every return point in
      `MatchingEngine::submit_limit`, `submit_market`, and `cancel`
      (`engine/matching_engine.cpp`). Thread it through the binary
      codec, text codec, and FIX adapter's ack/reject rendering,
      replacing whatever placeholder value each currently uses.
      Tests: submit two orders back-to-back before draining responses
      (or assert per-response order_id directly against engine unit
      tests); confirm each response's `order_id` matches its
      originating order, for both the accept path and each rejection
      path (duplicate ID, invalid qty/price, pool exhausted, STP,
      each risk-engine rejection). Existing tests that construct
      `EngineResponse` directly will need the new field added to their
      initializers — treat compile breaks here as an inventory of every
      call site touched.

- [x] **T3** — R3: exception boundary on the engine loop
      (design.md §3). Add `EngineResult::InternalError` to
      `core/Events.hpp`, following the existing reason-named,
      un-prefixed convention. Wrap the dispatch call inside
      `apps/exchange_server/main.cpp`'s engine loop in try/catch;
      on catch, construct an `InternalError` response tagged to the
      originating client, log to stderr, and continue the loop
      (queue draining and shutdown checks must proceed for subsequent
      commands — do not wrap the whole `while`, only the per-command
      dispatch, per design.md §3).
      Tests: inject a deliberately-throwing stub (e.g. a test-only
      `EngineAPI` implementation that throws on `submit`) and confirm
      the process does not terminate, the throwing client receives an
      `InternalError` response, and a subsequent, non-throwing command
      from a different client is still processed correctly afterward.

- [x] **T4** — R4: bounded write buffer, disconnect on overflow
      (design.md §4). Add `max_write_buffer_bytes` to `TcpServer`'s
      constructor parameters. In `send_to_client`
      (`adapters/tcp/tcp_server.cpp`), after appending to
      `conn.write_buffer`, check against the bound; on overflow, close
      and deregister that connection using the existing
      hard-close/cleanup path.
      Tests: construct a `TcpServer` with a small bound in a test
      fixture; drive a connection whose reads are paused/never drained
      while responses keep queuing for it; confirm the connection is
      closed once the bound is exceeded and that other connections'
      write buffers are unaffected.

---

## Syscall removal (independent of each other; both required before T9)

- [x] **T5** — R5: decouple feed publication from the matching call
      stack (design.md §5). Add a new POD event type (trade / order-
      accepted / order-cancelled fields with a discriminant) and an
      SPSC ring buffer of that type. Implement `QueuedEventSink`
      (implements `EventSink`, pushes to the new ring, drops and
      counts on full per the backpressure policy in design.md §5).
      Add a feed-publisher thread that owns the real
      `UdpFeedPublisher` and drains the new ring, calling
      `UdpFeedPublisher`'s existing, unmodified methods. Wire
      `QueuedEventSink` (not `UdpFeedPublisher` directly) as the
      `EventSink*` the composition root passes to `MatchingEngine`
      when the UDP feed is enabled.
      Tests: unit-test `QueuedEventSink` in isolation (push/drop-on-
      full/counter behavior) without a real socket. Integration test:
      confirm `UdpFeedPublisher`'s existing behavior (top-of-book
      publish-on-change, periodic snapshot) is unchanged when driven
      through the new queue+thread instead of directly — same
      assertions as the existing Phase 6 UDP feed tests, run against
      the new wiring. Confirm (by injecting a slow/blocking test
      double in place of the socket, or by call-count instrumentation)
      that `MatchingEngine::submit` returns without having invoked any
      blocking call — i.e. the engine-thread side of `QueuedEventSink`
      does only the ring push.

- [x] **T6** — R6: batch the eventfd notification
      (design.md §6). Move the `write(g_eventfd, ...)` call out of the
      per-command dispatch body in `apps/exchange_server/main.cpp`'s
      engine loop; issue it once per pass through the loop, only if at
      least one response was pushed since the last notification (see
      design.md §6 for the exact control-flow shape — notify when the
      inbound queue would otherwise go idle, not on a timer).
      Tests: existing response-routing/integration tests must still
      pass unmodified (the I/O thread's drain-to-exhaustion behavior
      is unaffected by *how many* times it's woken, only *that* it's
      woken before responses are needed). Add a test asserting that N
      commands submitted back-to-back (before the engine loop would
      otherwise go idle) produce fewer eventfd writes than N — direct
      unit test against the batching logic extracted to a testable
      unit, or an instrumented eventfd double if the loop can be
      driven in isolation.

---

## O(depth) / unbounded-state fixes

> Sequenced after T5/T6 only to avoid two people editing
> `MatchingEngine` concurrently with the composition-root changes; no
> real dependency exists (design.md §10).

- [x] **T7** — R7: replace `ever_seen_ids_` with a per-client
      monotonic watermark (design.md §7). Replace
      `std::unordered_set<OrderId> ever_seen_ids_`
      (`engine/matching_engine.hpp`) with
      `std::unordered_map<ClientId, OrderId> last_accepted_id_` (or
      final agreed name). Update the duplicate-ID check and insertion
      point in both `submit_limit` and `submit_market`
      (`engine/matching_engine.cpp`) per design.md §7's exact logic.
      Update every existing test that asserts *global* (cross-client)
      duplicate-ID rejection to instead assert the new *per-client*
      monotonic semantic — this is a deliberate, reviewable behavior
      change per requirements.md §7, not incidental breakage; call out
      each changed test explicitly in the PR/review.
      Tests: same `OrderId` from two different `ClientId`s is now
      accepted from both (new behavior — was previously rejected for
      the second). Same `OrderId` resubmitted by the *same* client is
      rejected. A lower `OrderId` submitted by a client after a higher
      one is rejected (monotonicity, not just uniqueness). Stress test:
      reproduce the research report's 200,000 add+cancel-cycle shape
      and confirm the tracking structure's size stays bounded by
      concurrent-client count rather than growing with cycle count —
      record the measurement in `benchmarks/results/`.

- [x] **T8** — R8: O(1) self-trade prevention (design.md §8).
      Add the per-client best-resting-price tracking structure
      alongside `OrderBook`, updated incrementally in
      `OrderBook::add_order`/`remove_order`'s call sites within
      `MatchingEngine`. Rewrite `would_self_cross`
      (`engine/matching_engine.cpp:285`) to use a lookup against this
      structure instead of scanning the crossable side. Call sites in
      `submit_limit`/`submit_market` (design.md §8) and the
      before-any-mutation ordering guarantee from Phase 8 are
      unchanged.
      Tests: **all existing Phase 8 STP tests must pass unmodified**
      (same-owner cross rejected with no ID consumed/no events/no
      fills; different-owner proceeds; STP-disabled proceeds;
      CancelResting ordering) — this task changes cost, not behavior.
      New test: reproduce the research report's depth-1/10/100/1000
      latency-vs-depth measurement shape and confirm the ratio between
      depth-1 and depth-1000 no longer scales with depth (expectation:
      near 1.0×, not ~50×) — record in `benchmarks/results/`.

---

## Verification

- [x] **T9** — R9: before/after syscall-count verification
      (design.md §9). Requires T5 and T6 both landed. On a real Linux
      host, run the exchange server under the Phase 2 load-generation
      harness (or a minimal extension of it), capture `perf trace -s`
      for a fixed duration on the pre-phase-11 build and on the
      post-T6 build under the same load shape and duration. Record the
      `sendto`/`write`/`sched_yield` counts from both runs in
      `benchmarks/results/phase-11-syscall-trace.md`. If this
      environment cannot execute a real Linux run (as Phase 8's T4
      encountered), document the reproduction steps, the expected
      result, and exactly what could not be captured here — do not
      silently omit this task's evidence, following Phase 8's
      precedent for the deferred T4 benchmark.
      No production code change — this task only records evidence for
      R5/R6's combined effect.

---

## Documentation

- [x] **T10** — Update `docs/LEARNING.md` with one entry per task
      (T1–T9), per `.kiro/steering/learning-doc.md` — if not already
      bundled into each task's own review per that steering doc's
      convention, this closes out any remaining entries.

- [x] **T11** — Update any client-facing protocol documentation that
      describes duplicate-`OrderId` rejection as global/cross-client,
      to reflect T7's new per-client monotonic semantic
      (requirements.md §7). Documentation-only, no behavior change.

---

## Sequencing notes

- **T1–T4 (correctness) have no dependencies on each other or on
  anything below** — parallelizable, or done in any order. They need
  no benchmark, no Linux host, no measurement to justify: the
  mechanism each one fixes is the argument (requirements.md §2).
- **T5 and T6 (syscall removal) are independent of each other** and of
  T1–T4. Both are prerequisites for T9.
- **T7 and T8 both touch `MatchingEngine`** and are sequenced
  back-to-back to avoid concurrent edits to the same file — there is
  no functional dependency between them (design.md §10).
- **T9 is gated on T5 and T6** — it measures their combined effect and
  cannot run meaningfully before both exist.
- **T10–T11 are documentation cleanup** and come last, matching every
  prior phase's convention.
- Nothing in this phase is gated on the `requirements.md` §5/§6
  deferred items (thread-count question, sequencer/journal, full risk
  suite, kernel-bypass networking) — this phase is fully completable
  without them, by design.
