# Phase 5 — Design: TCP Order Gateway

Implements `requirements.md` (Phase 5). Assumes Phase 4's SPSC queue
(`lockfree_queue/`, class `SpscRingBuffer`) and
`EngineCommand`/`EngineResponse` types from `core/`.

## 1. Architecture

Two threads, two queues, both SPSC (preserving Phase 4's invariant):

```
[I/O thread]                        [Engine thread]
  epoll loop                          loop {
  ├─ accept() → new Connection          cmd = inbound.try_pop()
  ├─ read/parse → TaggedCommand         engine.process(cmd) → resp
  │    └─ inbound.push ───────────▶     if (resp) outbound.push ──┐
  ├─ eventfd wakeup ◄───────────────────┴─ eventfd write per push  │
  ├─ drain outbound: resp → serialize → conn->send buffer          │
  └─ flush write buffers (EPOLLOUT)                                │
                                                       ┌───────────┘
                                              outbound SPSC queue
```

- **Inbound:** `SpscRingBuffer<TaggedCommand, N>` (Phase 4 queue,
  reused as-is).
- **Outbound:** `SpscRingBuffer<TaggedResponse, 65536>` — same class,
  opposite direction (see §6 for the capacity choice).
- **Notification:** `eventfd` registered with the same epoll instance
  (see §5).

### Connection model

```cpp
struct Connection {
    int fd;                       // nonblocking, TCP_NODELAY
    ClientId id;                  // ephemeral, assigned at accept()
    std::string read_buffer;      // partial frames accumulate here
    std::string write_buffer;     // unsent response bytes
};
```

Owned by the I/O thread exclusively (single-threaded epoll loop —
no connection-level locking). `ClientId` is assigned from a
monotonically increasing counter at accept time.

## 2. `adapters/text_protocol/` — extracted shared parser

Extracted from `apps/cli/` (trigger: actual reuse, per
`.kiro/steering/structure.md`). Covers only engine-facing commands:

```cpp
// Returns only engine-facing commands. PRINT_BOOK/QUIT are CLI-only
// and handled by apps/cli/ before reaching this layer.
std::variant<EngineCommand, ParseError> parse(std::string_view line);

// No render(const OrderBook&) — book rendering stays in apps/cli/.
std::string render(const EngineResponse&);
```

- `EngineCommand` here means the variant `LimitOrder | MarketOrder |
  CancelRequest` from Phase 4 — no extension needed.
- `apps/cli/` refactor: CLI keeps its own richer parse layer that
  intercepts `PRINT_BOOK` / `QUIT` locally, then delegates
  order/cancel lines to `text_protocol::parse`.
- Framing (length-prefix) is a *transport* concern and lives in
  `adapters/tcp/`, not in the parser.

## 3. Queue payloads

```cpp
// In core/Types.hpp — strong wrapper, consistent with the
// OrderId/Price/Quantity/Sequence convention (explicit constructor,
// ==/!=, hash support — same shape as OrderId). Ephemeral: one per
// accepted connection, assigned by the I/O thread, never persisted.
struct ClientId {
    uint64_t value;
    explicit constexpr ClientId(uint64_t v) : value(v) {}
};

struct TaggedCommand {
    ClientId client;
    EngineCommand command;
};

struct TaggedResponse {
    ClientId client;
    EngineResponse response;   // moves; vector<Trade> transfers by ownership
};
```

Response routing: the engine thread pushes every `TaggedResponse`
onto the outbound queue; the I/O thread looks up the connection by
`ClientId` in an open-connections map and appends the serialized
response to that connection's write buffer. If the client has
disconnected, the response is dropped (connection gone — nothing to
route to; logged at debug level).

## 4. I/O loop (edge-triggered, nonblocking)

Per iteration:

1. `epoll_wait(-1)` — blocks indefinitely, woken by client events, the
   eventfd, or shutdown (which writes to the same eventfd, see §5).
   No polling timeout; shutdown responsiveness is event-driven, not
   time-based.
2. Listener fd readable → `accept4(..., SOCK_NONBLOCK)` in a loop
   until `EAGAIN` (edge-triggered drain rule applies to the listener
   too). Set `TCP_NODELAY`, create `Connection`, assign `ClientId`,
   register with epoll (`EPOLLIN | EPOLLET`).
3. Client fd readable → `read()` in a loop until `EAGAIN`/EOF,
   appending to `read_buffer`. After each complete frame
   (4-byte BE length + payload), parse via `text_protocol::parse`
   and push `TaggedCommand` to inbound. On `ParseError`: serialize an
   error response directly to that connection's write buffer (no
   engine round-trip).
4. eventfd readable → read the counter (clears it). If the shutdown
   flag is set, exit the loop; otherwise drain the outbound queue
   with `try_pop()` until it returns false, appending each response's
   serialized bytes to the matching connection's write buffer.
5. Flush all write buffers with `write()` loops until `EAGAIN` or
   empty. If a buffer still has data, ensure `EPOLLOUT` is armed for
   that fd; disarm when the buffer drains (avoid busy-looping on
   always-writable sockets).
6. `EPOLLHUP`/`EPOLLERR`/EOF → close, erase connection. (Half-closed
   handling per requirements §6: stop accepting inbound, flush if
   writable, else drop.)

Read buffer policy: per-connection `std::string` reused across reads
(`clear()` keeps capacity) — no per-message allocation on the hot path
beyond the payload itself.

## 5. eventfd notification semantics

- Engine thread: after each successful `outbound.push()`, write 1 to
  the eventfd. Multiple pushes may coalesce in the counter — harmless,
  because the I/O thread always drains to exhaustion rather than
  popping once per notification.
- I/O thread: on eventfd wakeup, `read()` the 8-byte counter (resets
  it), then `try_pop()` in a loop until empty. Edge-triggered on the
  eventfd as well; the read-then-drain pattern satisfies the
  drain invariant.
- **Shutdown writes to the same eventfd** (one write from the signal
  path after setting the atomic flag) — this unblocks the I/O thread
  from `epoll_wait(-1)`. Because the counter may already be nonzero,
  the wakeup handler checks the shutdown flag *before* draining, so a
  coalesced shutdown+notification wakeup still terminates. No signalfd
  needed at this scale.
- Inbound direction: the engine thread uses a bounded spin (Phase 4's
  existing wait strategy) — no eventfd needed engine-ward.

## 6. Queue sizing

- Outbound: `SpscRingBuffer<TaggedResponse, 65536>` — capacity is a
  compile-time template parameter (power-of-two, per Phase 4's
  discipline), instantiated explicitly here. 65536 vs. inbound's
  Phase 4 size, because responses are the burstier side (a fill can
  emit a trade + an ack per order) and the engine must never block
  indefinitely on push under normal load (R8). Spin-retry on full is
  retained as the last-resort back-pressure, documented in the ADR.

## 7. `apps/exchange_server/` (new composition root)

A new executable — the first multi-threaded, network-facing app. Not
folded into `apps/cli/` or `apps/benchmark/` (different shape from
either):

```cpp
// apps/exchange_server/main.cpp (sketch)
main():
    construct MatchingEngine (with NullEventSink or a future Phase 6 sink)
    construct SpscRingBuffer<TaggedCommand, 4096>   inbound
    construct SpscRingBuffer<TaggedResponse, 65536>  outbound
    create eventfd
    construct TcpServer (given inbound queue ref, outbound queue ref,
                          eventfd fd, text_protocol parser/renderer)
    install SIGINT/SIGTERM handler → sets atomic shutdown flag,
                                     writes 1 to eventfd
    spawn I/O thread running TcpServer::run() (epoll loop)
    engine thread (main thread):
        loop:
            if shutdown flag set → break
            TaggedCommand cmd;
            if inbound.try_pop(cmd):
                EngineResponse resp = std::visit(dispatch to engine)
                TaggedResponse tagged{cmd.client, std::move(resp)}
                while (!outbound.try_push(std::move(tagged))) {}  // R8 spin
                write(eventfd, 1)  // notify I/O thread
    join I/O thread


## 8. Judgment calls (documented, revisitable)

1. **Single epoll instance for listener + clients + eventfd** — one
   loop, one code path. A separate wakefd per direction is overkill
   at this scale.
2. **`ClientId` as a strong wrapper in `core/Types.hpp`** — not a bare
   `uint64_t` alias. Keeping it type-safe now prevents Phase 8/9 from
   inheriting an ambiguity between client IDs and order IDs. Lives in
   core because three phases consume it, even though Phase 5 is the
   first *user*.
3. **Error responses in-band as text** (`"ERROR <reason>"` payload) —
   same grammar as success responses, no separate error channel.
4. **No `SO_REUSEPORT` / multiple acceptors** — single I/O thread is
   deliberate (see §8); scaling acceptors is out of scope.
5. **Shutdown via eventfd write, not a poll timeout** — keeps
   `epoll_wait(-1)` (zero idle overhead) while remaining responsive;
   the alternative (100ms poll timeout) was rejected as a wasteful
   fixed-interval wakeup on a latency-focused project.

## 9. Why epoll + edge-triggered + two SPSC queues — not the alternatives

- **Why not `io_uring`?** Lower syscall overhead in theory, but
  significantly more complex submission/completion ring setup, and
  less battle-tested for this project's simple accept-read-write
  pattern. epoll is the well-understood default; io_uring is a
  later-phase optimization candidate if the benchmark shows syscall
  overhead matters (unlikely given the engine is the bottleneck, not
  I/O).
- **Why not `poll`/`select`?** O(n) per call — must scan the entire fd
  set every iteration. epoll is O(1) amortized per ready event. With
  potentially dozens of concurrent clients, this matters.
- **Why not one thread per connection?** Thread-per-client means N
  kernel threads, context-switch overhead between them, and (worse)
  the need to synchronize N threads pushing onto one queue — turning
  this into an MPSC problem that Phase 4 explicitly deferred. A single
  I/O thread with epoll keeps the SPSC invariant intact.
- **Why not shared memory / IPC instead of in-process queues?** The
  engine and I/O thread live in the same process — shared memory adds
  complexity (mmap, cleanup, permissions) with zero benefit when a
  ring buffer in the same address space already achieves the same
  cache-line-level communication. Shared-memory IPC is for
  cross-process boundaries that don't exist here.
