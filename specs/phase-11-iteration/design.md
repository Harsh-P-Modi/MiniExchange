# Phase 11 — Design: Iteration

Status: **APPROVED** — `tasks.md` is written from this version.

Builds on the resolved `requirements.md`: Q1 (feed decoupling shape), Q2
(watermark structure), Q3 (STP index), Q4 (payload flattening deferred),
Q5 (thread-count question deferred) are settled. This document is
organized by requirement, in the order `tasks.md` implements them
(correctness first, then syscall removal, then O(depth)/unbounded-state
fixes, then verification).

---

## 1. R1 — explicit rejection on inbound queue full

**Current code** (`apps/exchange_server/main.cpp:213-219`):

```cpp
} else {
    // Valid command: push to inbound queue for engine.
    // Drop on full (R4 — bounded queue, no blocking).
    TaggedCommand cmd{client_id, val};
    inbound.try_push(std::move(cmd));
}
```

**Change:** check the return value. On `false`, render and send a
rejection using the same path already used for `ParseError` a few lines
above it:

```cpp
} else {
    TaggedCommand cmd{client_id, val};
    if (!inbound.try_push(std::move(cmd))) {
        auto err_msg = render_error_fn("system busy — order not accepted");
        server.send_to_client(client_id, frame_message(err_msg));
    }
}
```

This reuses `render_error_fn`/`frame_message`/`send_to_client` — no new
wire format, no new codec path. The rejection is indistinguishable on
the wire from a protocol-level parse error today; if a future phase
wants a distinct `EngineResult`-style reason code for this case (as
opposed to a malformed message), that's a codec extension, not part of
this fix.

**Why not push a distinct EngineResult through the engine instead:**
the queue is full precisely because the engine is not keeping up —
routing this rejection *through* the engine would require space in the
same queue that's already full. Responding directly from the I/O thread,
which is what the existing `ParseError` path already does, is the only
option that doesn't depend on the resource that's exhausted.

## 2. R2 — `OrderId` on the response

**Current shape** (`core/Events.hpp`):

```cpp
struct EngineResponse {
    EngineResult status;
    std::vector<Trade> trades;
    Quantity remaining_qty;
};
```

**Change:** add `OrderId order_id;`. `MatchingEngine::submit_limit` and
`submit_market` (`engine/matching_engine.cpp`) already have `order.id`
in scope at every return point (validation failures, `PoolExhausted`,
`SelfTradePrevented`, and the success path) — set `resp.order_id =
order.id` (or the relevant id for a `cancel()` rejection/success) at
each. `cancel()`'s response also gets `order_id` set from its
`OrderId` argument, which it already has.

This is an additive field on an existing struct — `TaggedResponse`
(`core/TaggedCommand.hpp`), the binary codec, the text codec, and the
FIX adapter all currently construct/consume `EngineResponse` without
this field populated; each of them gets a one-line addition to read or
render it. The binary/text gateway codecs currently synthesize a
placeholder order id for their ack/reject wire messages — this task
replaces the placeholder with the real value; the wire *format* is
unchanged, only what fills that existing field.

**Why not a side-channel correlation ID instead:** the order already
has a stable identity (`OrderId`, client-supplied and unique per Q2 in
`requirements.md`). Introducing a second identifier to solve a problem
the first one already solves would be needless indirection.

## 3. R3 — exception boundary on the engine loop

**Current code** (`apps/exchange_server/main.cpp:249-296`, inside the
`while (!g_shutdown...)` loop): the dispatch call
(`engine.submit(command)` / `engine.cancel(command.id)`) is unguarded.

**Change:** wrap the dispatch in try/catch inside the loop body, not
around the whole loop (so a caught exception doesn't skip queue
draining or shutdown checks for subsequent iterations):

```cpp
EngineResponse resp;
try {
    resp = std::visit(/* ... existing dispatch ... */, cmd.command);
} catch (const std::exception& e) {
    resp = EngineResponse{EngineResult::InternalError, {}, Quantity{0},
                           /* order_id from cmd, if extractable */};
    std::fprintf(stderr, "engine dispatch threw for client %llu: %s\n",
                 static_cast<unsigned long long>(cmd.client.value), e.what());
}
```

This requires one new `EngineResult::InternalError` value
(`core/Events.hpp`), following the existing reason-named,
un-prefixed convention (matches Phase 8's `PriceOutOfBand` etc.). The
response still routes back to the client that caused it — the
Charter's "engine returns structured results, never throws for
expected business outcomes" already implies unexpected ones (a
genuine bug, an invariant violation) are the only thing that should
reach this catch; catching here converts "kills the venue" into "one
client gets an internal-error response and the venue keeps running,"
which is a strict improvement even though the ideal outcome is that
this catch is never exercised in practice.

**Why not `noexcept` on `EngineAPI::submit`/`cancel` instead:** that
would upgrade any throw to `std::terminate` immediately at the point of
the throw, which is worse, not better — it removes the option to
catch. The catch belongs at the composition-root boundary that already
owns per-response error handling, not inside the engine.

## 4. R4 — bounded write buffer, disconnect on overflow

**Current code:** `Connection::write_buffer`
(`adapters/tcp/tcp_server.hpp:38`) is an unbounded `std::string`;
`send_to_client` (`adapters/tcp/tcp_server.cpp:404-414`) appends to it
unconditionally, and `flush_write_buffer`
(`adapters/tcp/tcp_server.cpp:363-...`) drains what the kernel will
accept.

**Change:**

- Add a configurable `max_write_buffer_bytes` to `TcpServer`'s
  construction parameters (constructor DI, matching the project's
  existing configurable-port convention from Phase 5).
- In `send_to_client`, after `conn.write_buffer.append(data)`, check
  `conn.write_buffer.size() > max_write_buffer_bytes`; if so, close
  that connection's fd, deregister it from epoll, and erase its
  `Connection` entry — the same cleanup path already used for a
  hard-close on a malformed/oversized-frame client, so this reuses
  existing connection-teardown logic rather than adding a new one.

**Why disconnect rather than backpressure the read side:** a
production venue could instead stop reading from a slow client to let
its send buffer drain, which is a more sophisticated and more
forgiving policy. This phase chooses the simpler, blunter policy
(disconnect) because it is the one the current architecture already
supports without new state, and it converts an unbounded-memory
correctness bug into a bounded, observable one. A backpressure policy
is a reasonable future refinement, not required to close R4.

## 5. R5 — decouple feed publication from the matching call stack

**Current call graph:**

```
MatchingEngine::match_against_book (engine/matching_engine.cpp:260)
  └─ sink_->on_trade(trade)               [EventSink port call]
        └─ UdpFeedPublisher::on_trade      [concrete adapter, prod wiring]
              └─ ::sendto(...)             [blocking syscall, ON the engine's stack]
```

`sink_` is an `EventSink*` — the port itself is not the problem (it is
exactly the abstraction the Charter calls for: the engine doesn't know
UdpFeedPublisher exists). The problem is that the concrete adapter
wired into the composition root performs I/O synchronously inside the
callback the engine invokes on its own stack.

**Change:** introduce a queue-backed `EventSink` implementation that
sits between the engine and `UdpFeedPublisher`:

```
MatchingEngine
  └─ sink_->on_trade(trade)                    [same port, same call]
        └─ QueuedEventSink::on_trade            [NEW — engine-thread side]
              └─ event_queue.try_push(evt)       [SPSC push, ~tens of ns, no syscall]
                                                   (engine thread returns here)

                                                  [separate thread]
FeedPublisherThread::run()
  └─ while (event_queue.try_pop(evt)) { ... }
        └─ UdpFeedPublisher::on_trade(evt)        [unchanged — still calls sendto]
```

- `QueuedEventSink` implements `EventSink` (same port `MatchingEngine`
  already calls through — no engine-side change beyond what gets
  constructed in the composition root) and pushes a small POD event
  struct (trade fields, or order-accepted/cancelled fields, tagged
  with a discriminant) into a new SPSC ring buffer, sized similarly to
  the existing outbound queue.
- A new thread (spawned alongside the existing I/O and engine threads
  in `main.cpp`) owns the real `UdpFeedPublisher` instance and drains
  this queue, calling the existing, unmodified `UdpFeedPublisher`
  methods from *its* stack instead of the engine's.
- **Backpressure policy, matching R1's precedent:** if this queue is
  full, drop the event (a stale/missed market-data tick is the
  correct failure mode for a feed — the research report's own
  market-data section agrees dropping is right, just that it must be
  *counted*). Increment a dropped-event counter exposed for
  diagnostics; do not block the engine thread waiting for space.
- `NullEventSink` (used by benchmarks/tests that don't care about
  events) is unaffected — this only changes what the *composition
  root* wires in for the production UDP feed.

**Why not just have `UdpFeedPublisher::on_trade` push the raw UDP
packet bytes to a queue instead of adding a new sink layer:**
`QueuedEventSink` stays a generic `EventSink`, so the queue holds
engine-domain events (a `Trade`, an `OrderAccepted`), not
already-serialized UDP payloads. This keeps `UdpFeedPublisher`'s
existing state-reconstruction logic (best bid/ask tracking, the
qty-vs-count level-drained distinction) unchanged and running on its
own thread exactly as it runs today — only *where* it runs moves, not
*what* it does. Serializing to raw bytes on the engine thread instead
would just relocate a smaller amount of the same problem.

**Why not remove `EventSink` and have the engine push directly to this
new queue:** that would special-case one particular consumer (a UDP
feed) into the engine's own type, breaking the reason `EventSink`
exists — it also serves logger/benchmark-counter observers (per
`.kiro/steering/tech.md`) that have no reason to be queue-based. The
port stays generic; only the concrete production wiring changes.

## 6. R6 — batch the eventfd notification

**Current code** (`apps/exchange_server/main.cpp:283-298`): inside the
per-command loop body, after every single `outbound.try_push`, one
`::write(g_eventfd, &val, sizeof(val))` fires.

**Change:** move the `write()` out of the per-command dispatch and
issue it once per pass through the outer `while` loop, only if at least
one response was pushed since the last notification:

```cpp
bool responded_this_pass = false;
while (!g_shutdown.load(std::memory_order_relaxed)) {
    TaggedCommand cmd{};
    if (!inbound.try_pop(cmd)) {
        if (responded_this_pass) {
            uint64_t val = 1;
            [[maybe_unused]] auto _ = ::write(g_eventfd, &val, sizeof(val));
            responded_this_pass = false;
        }
        std::this_thread::yield();
        continue;
    }
    // ... existing dispatch + outbound.try_push ...
    responded_this_pass = true;
}
```

This coalesces the eventfd write to at most one per "drain the inbound
queue until empty" cycle, which is exactly the granularity at which
the I/O thread's drain handler already operates (`set_response_drain_handler`
drains `outbound` to exhaustion in one call, `main.cpp:227-238`) — so
batching the notification to match doesn't change how many responses
get flushed per wakeup, only how many wakeups are requested. Under
load (many commands arriving between drains), this collapses many
syscalls into one; under light load (one command, then idle), behavior
is unchanged from today.

**Why not a time-based or count-based batching window instead:**
that would trade a fixed, bounded latency cost (this change adds none
— the write already happens before the loop goes idle) for an
unbounded one (waiting up to N ms or N responses before notifying).
Notifying "whenever the queue would otherwise go idle" gets the full
benefit under load with no added latency under light load.

## 7. R7 — bounded duplicate-ID tracking (per-client watermark)

**Current state** (`engine/matching_engine.hpp:51`,
`engine/matching_engine.cpp:66,91,129,144`):
`std::unordered_set<OrderId> ever_seen_ids_`, checked via `.contains()`
and grown via `.insert()`, once per accepted order, forever.

**Change:** replace with `std::unordered_map<ClientId, OrderId>
last_accepted_id_` (name indicative; final naming in code review). On
submit:

```cpp
auto it = last_accepted_id_.find(order.owner);
if (it != last_accepted_id_.end() && order.id <= it->second) {
    return EngineResponse{EngineResult::DuplicateOrderId, {}, Quantity{0}, order.id};
}
// ... existing validation/pool/STP checks ...
last_accepted_id_[order.owner] = order.id;  // replaces ever_seen_ids_.insert
```

This is O(1) per check (one hash lookup keyed by `ClientId`, of which
there are boundedly many concurrent clients — not boundedly many
orders), and the map's size is bounded by concurrent-client count
rather than growing with every order ever submitted. It requires
`order.owner` to be populated at the point of this check, which it
already is (Phase 8's `ClientId` retrofit — `order.owner` is set
before `submit_limit`/`submit_market` run, per
`engine/matching_engine.cpp`'s existing flow).

**Compatibility note (ties to requirements.md §7):** this is a
semantic narrowing from global to per-client monotonic uniqueness.
Existing tests asserting global uniqueness across different
`ClientId`s must be updated as part of this task, not treated as
incidental breakage — `tasks.md` calls this out as its own step so the
change in behavior gets its own review, not a drive-by edit buried in
a bigger diff.

**Why not the bounded-ring-plus-Bloom-filter option:** it keeps global
(cross-client) uniqueness semantics exactly, at the cost of a
nonzero, if small, false-duplicate-rejection rate and a more complex
structure (two data structures, an eviction policy). The watermark is
simpler, exactly correct for the semantics it provides, and requires
no probabilistic reasoning about false-positive rates — the tradeoff
this phase accepts is the narrower semantic (see above), which is a
one-time, reviewable, documented change rather than an ongoing
approximation.

**Why not session-scoped IDs (reset on reconnect) instead:** that
requires the connection/session-lifecycle machinery
(`requirements.md` §6 in the research-report follow-up list, i.e.
session layer / reconnect handling) that this project does not have
yet and that is explicitly out of scope for this phase. The watermark
does not require a session concept — it only requires `ClientId`,
which already exists.

## 8. R8 — O(1) self-trade prevention

**Current code** (`engine/matching_engine.cpp:285`,
`would_self_cross`): scans the crossable opposite-side price levels
looking for a resting order owned by `order.owner`, called from both
`submit_limit` (`:76`) and `submit_market` (`:138`) as a pre-scan
before any mutation (preserving the ordering guarantee design.md §5 of
Phase 8 established — this phase does not change *when* the check
runs, only *how*).

**Change:** maintain a small per-client structure alongside
`OrderBook`, updated incrementally wherever an order is inserted or
removed (both already O(1) operations — `OrderBook::add_order` /
`remove_order`):

- For each `ClientId` with resting orders, track that client's best
  (highest) resting bid price and best (lowest) resting ask price —
  two `optional<Price>` per client, updated on insert/remove of that
  client's own resting orders.
- `would_self_cross(side, owner, price)` becomes: look up `owner`'s
  tracked opposite-side best price; if present, compare against
  `price` (or, for a market order sweeping the whole book, against
  "does this client have *any* resting order on the opposite side" —
  a nullopt check) — O(1) regardless of book depth.

This preserves the exact same call sites and the exact same ordering
guarantee (pre-scan before `ever_seen_ids_`/watermark insertion and
before `on_order_accepted`) — only the internals of
`would_self_cross` change, and the incremental-update hooks live where
`OrderBook::add_order`/`remove_order` are already called from
`MatchingEngine`, not as a new traversal.

**Why not the per-level-owner-count alternative the research report
also considered:** that is O(levels-crossed), which is a real
improvement over O(depth × orders-per-level) but is still a function
of how much of the book the incoming order would cross — a market
order sweeping 500 levels still pays for 500 lookups. The per-client
best-price index is genuinely O(1) regardless of sweep size, which is
what R8 requires.

**Why this doesn't reopen the "decorator vs. engine" argument from
Phase 8:** Phase 8's `design.md` §5 gave three reasons STP must live
in the engine's match loop rather than the `RiskEngine` decorator:
duplicated crossing logic, cost, and mutation-ordering. None of those
reasons depended on the *scan* being O(depth) — they depended on STP
needing to run before any state mutation, which an O(1) lookup still
does. This phase does not move STP out of the engine; it only changes
how the in-engine check is computed.

## 9. R9 — verification methodology

- Run the exchange server on a real Linux host (not this project's
  Windows dev environment) under a synthetic load generator that
  submits enough crossing orders to produce trades and top-of-book
  changes (reusing/extending the Phase 2 benchmark harness's load
  generation, not writing a new one).
- Capture `perf trace -s -p <pid>` for a fixed duration **before**
  R5/R6 land (on the pre-phase-11 build) and **after** (on the
  post-R6 build), same load shape, same duration.
- Record the syscall histogram from both runs — specifically the
  counts for `sendto`, `write`, and `sched_yield` — in
  `benchmarks/results/phase-11-syscall-trace.md`, following the format
  Phase 8 used for its own before/after documentation
  (`benchmarks/results/phase-08-order-size.md`), including an honest
  note about which parts of this could and could not be executed in
  this dev environment (matching Phase 8's precedent for the T4
  benchmark that had to be deferred to a controlled Linux run).
- This is the phase's actual acceptance evidence — not a synthetic
  single-process micro-benchmark, and not a re-citation of the
  research report's own numbers (NFR2).

## 10. Sequencing and Dependencies

```
R1, R2, R3, R4   — independent of each other and of everything below;
                   no shared state, can be done in any order or in parallel.

R7               — independent; touches only MatchingEngine's dup-ID check.

R8               — independent of R7 (different structures), but touches
                   the same class (MatchingEngine) and the same call sites
                   (submit_limit/submit_market pre-scan) as R7 — sequenced
                   after R7 in tasks.md purely to avoid two people editing
                   the same functions at once, not because of a real
                   dependency.

R5               — independent; touches EventSink wiring in the
                   composition root and adds a new thread + queue.
                   Does not require R6.

R6               — independent; touches only the engine loop's
                   notification timing. Does not require R5.

R9               — depends on R5 AND R6 both landing (it measures their
                   combined effect) and requires a Linux host.
```

No task in this phase depends on the deferred items in
`requirements.md` §5 (thread-count question) or §6 (out of scope
list) — this phase is fully specifiable and completable without
resolving either.

## 11. Definition of Done

- R1–R9 implemented; every fix covered by tests including the specific
  failure mode it closes (see `requirements.md` §4).
- `EngineResult::InternalError` added, following the existing
  reason-named convention.
- `OrderId` threads through `EngineResponse` → `TaggedResponse` →
  both gateway codecs (binary, text) → FIX adapter, replacing
  placeholder values.
- Per-client watermark replaces `ever_seen_ids_`; existing
  cross-client duplicate-ID tests updated to reflect the new,
  narrower, documented semantic (requirements.md §7).
- `would_self_cross` is O(1); STP tests from Phase 8 (same-owner
  reject, different-owner proceeds, STP-disabled proceeds,
  CancelResting ordering) all still pass unmodified — this phase must
  not change STP's *behavior*, only its *cost*.
- `QueuedEventSink` + feed-publisher thread wired into the composition
  root; `UdpFeedPublisher` itself is unmodified (only what calls it
  changes).
- eventfd notification batched per drain cycle.
- `benchmarks/results/phase-11-syscall-trace.md` records the R9
  before/after comparison, or documents precisely what could not be
  captured in this environment and why (matching Phase 8's precedent
  rather than silently omitting it).
- `docs/LEARNING.md` updated per steering.
