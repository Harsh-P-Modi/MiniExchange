# Phase 5 — Design: TCP Order Gateway

Status: **APPROVED** — `tasks.md` is written from this version.

## 1. Overview

Two threads: the TCP I/O thread (epoll) and the engine thread (Phase
1's `MatchingEngine`, still single-threaded internally). Two Phase 4
`SpscRingBuffer`s connect them — one each direction — since a
synchronous `submit()` return value can no longer serve as "the
response," once the call is decoupled across threads. A new
`apps/exchange_server/` composition root wires everything together.
The CLI's plaintext grammar gets extracted into a shared library since
it's now used by two things.

## 2. `adapters/text_protocol/` (extracted from Phase 1's `apps/cli/`)

```cpp
class TextProtocolParser {
public:
    // Returns EngineCommand on success; a ParseError with a specific
    // reason on failure (malformed command, wrong arg count, unknown
    // verb) — same "specific reason, not generic failure" discipline
    // as everywhere else in this project.
    std::variant<EngineCommand, ParseError> parse(std::string_view line);
};

class TextProtocolRenderer {
public:
    std::string render(const EngineResponse&);
    std::string render(const OrderBook&);   // PRINT_BOOK
};
```

`apps/cli/` is refactored to depend on this library instead of owning
the grammar itself — `apps/cli/main.cpp` becomes purely: read a line,
`TextProtocolParser::parse`, call `EngineAPI` directly (in-process, no
queue — Phase 1's CLI stays synchronous, it was never threaded),
`TextProtocolRenderer::render` the result. `adapters/tcp/` uses the
*same* parser/renderer, over the network, asynchronously via the
queues in §3.

**This is exactly the situation `structure.md` describes as the
trigger for extraction — not a speculative "might be reused someday"
call.**

## 3. `core/TaggedCommand.hpp` (new — the cross-thread message shapes)

```cpp
// ClientId lives in core/Types.hpp (Phase 1's type list, extended here).
// A strong wrapper struct, NOT a bare `using ClientId = uint64_t;` alias —
// see the Open Questions resolution and ADR-006. A plain alias would let
// ClientIds and OrderIds (also uint64_t-shaped) interchange unnoticed.
struct ClientId {
    uint64_t value;
    constexpr ClientId() : value(0) {}
    explicit constexpr ClientId(uint64_t v) : value(v) {}
    // ==, != , plus a std::hash specialization for unordered_map keys.
};

struct TaggedCommand {
    ClientId client;
    EngineCommand command;   // Phase 4's core/EngineCommand.hpp
};

struct TaggedResponse {
    ClientId client;
    EngineResponse response;   // Phase 1's core/Events.hpp
};
```

> **Corrected in Phase 8 (T14).** An earlier draft of this section showed
> `using ClientId = uint64_t;` and named the fields `client_id`. That was
> never what shipped — the implementation uses the strong wrapper struct
> above with fields named `client` (see `core/Types.hpp` and
> `core/TaggedCommand.hpp`). The stale snippet was corrected while Phase 8
> was consuming `ClientId` for self-trade prevention, so the spec matches
> the code.

## 4. Two queues, not one

```
TCP I/O thread                          Engine thread
      │  SpscRingBuffer<TaggedCommand>       │
      ├───────────────────────────────────▶  │  (Phase 4 queue,
      │                                      │   inbound)
      │  SpscRingBuffer<TaggedResponse>      │
      │  ◀───────────────────────────────────┤  (a second instance
      │                                      │   of the same Phase 4
                                              │   template, outbound)
```

**Why two, not one bidirectional structure:** `SpscRingBuffer` is
single-producer-single-consumer *per instance*. The inbound direction
has the TCP thread as producer, engine thread as consumer; the
outbound direction has the reverse roles. That's two distinct SPSC
relationships, so it's two instances of the same template — not one
queue trying to serve two different producer/consumer role
assignments, which wouldn't be SPSC anymore.

## 5. `adapters/tcp/TcpServer.hpp`

- One epoll instance, edge-triggered, nonblocking, on the I/O thread
  (R1, R3).
- Each accepted connection: `fd`, an assigned `ClientId` (a plain
  incrementing counter — no atomics needed, since only the I/O thread
  ever accepts connections and assigns IDs), a growable read buffer for
  partial-frame accumulation, and a write queue for outbound bytes not
  yet flushed to a non-blocking socket.
- `TCP_NODELAY` set immediately after `accept()` (R2).
- Framing (R4): 4-byte big-endian length prefix + payload. A **max
  frame size** (e.g. 4 KB) is enforced — a length prefix claiming more
  than that disconnects the client. This isn't in `requirements.md`
  explicitly, but it's a necessary NFR2 consequence: without a cap, a
  malformed or malicious client claiming a huge length could exhaust
  server memory one connection at a time.
- **Outbound wakeup: `eventfd`, not a short epoll timeout.** The
  engine thread, after pushing onto the outbound `TaggedResponse`
  queue, writes to an `eventfd` that the I/O thread has registered
  alongside client sockets in the same epoll set. This lets
  `epoll_wait` block indefinitely (no busy-polling, no wasted CPU)
  except when there's actually a client event *or* a response to flush
  — the idiomatic Linux way to integrate a cross-thread queue into an
  epoll loop, rather than polling the queue on a fixed short timeout.

## 6. `apps/exchange_server/` (new composition root)

```
main.cpp:
    construct MatchingEngine
    construct the two SpscRingBuffer<...> instances
    construct TcpServer (given the inbound queue to push into,
                          the outbound queue + eventfd to drain)
    spawn TCP I/O thread running TcpServer's epoll loop
    engine thread (main thread, or a second spawned thread):
        loop:
            try_pop from inbound queue
            if got a TaggedCommand:
                std::visit dispatch to engine.submit(...)/cancel(...)
                wrap result + client_id into TaggedResponse
                try_push onto outbound queue
            (busy-spin per Phase 4's design — no yield; see judgment
             call §7 item 3)
```

This is a new app, not folded into `apps/cli/` or `apps/replay/` —
it's the first genuinely multi-threaded, network-facing composition
root, a different shape from either.

## 7. Judgment calls made here — flag if any should change

1. **`eventfd`-based wakeup over a fixed short epoll timeout.** More
   correct and more idiomatic, at the cost of one more moving part
   (registering and draining an eventfd) than "just poll every 1ms."
   Worth it given this project's whole premise is caring about exactly
   this kind of latency/CPU-efficiency detail.
2. **`ClientId` as a strong wrapper struct**, not a plain `uint64_t`
   alias. Keeping it type-safe prevents `ClientId` and `OrderId` (also a
   `uint64_t`-shaped value) from interchanging unnoticed, consistent with
   the existing `OrderId`/`Price`/`Quantity`/`Sequence` convention. See
   ADR-006.
   *(Corrected in Phase 8 / T14: this item previously described the
   opposite decision — a plain alias — which was an early draft that the
   implementation never followed. Phase 8 threads `ClientId` into the
   engine for self-trade prevention, and the strong type is exactly what
   made that retrofit safe, so the record is now accurate.)*
3. **Engine thread busy-spins on the inbound queue**, no yield/sleep
   when idle. Matches the "pin one instrument to one dedicated thread"
   HFT philosophy from the Charter, at the cost of burning a full CPU
   core continuously even when idle — a real tradeoff worth naming
   explicitly for a dev machine (vs. a dedicated trading box where this
   is simply correct and expected).
4. **Max frame size (4 KB) is a hardcoded constant** for this phase,
   not yet configurable — simplest thing that satisfies the NFR2
   consequence identified in §5; revisit if a later phase needs a
   different limit.

---

Once approved, `tasks.md` breaks this into steps: extract
`adapters/text_protocol/` and refactor `apps/cli/` onto it first (proves
the extraction is behavior-preserving before anything new is built on
top of it), then `TaggedCommand`/`ClientId`, then `TcpServer` itself,
then `apps/exchange_server/`, then the round-trip latency benchmark.
