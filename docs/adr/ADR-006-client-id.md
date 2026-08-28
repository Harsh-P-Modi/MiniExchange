# ADR-006: ClientId — Typed Per-Connection Identity for Response Routing

**Status:** Accepted  
**Date:** Phase 5

## Context

Phase 5 introduces the TCP gateway — the first phase with multiple
concurrent clients. Two new problems arise that Phase 1's single-user
CLI never faced:

1. **Response routing:** When Client A submits an order and Client B
   submits another, the engine processes both and produces two
   responses. Those responses must be routed back to the *correct*
   client — A gets A's response, B gets B's. The mechanism needs an
   identifier that tags each command with its origin and each response
   with its destination.

2. **Phase 8 forward-dependency:** Phase 8 (risk engine) introduces
   self-trade prevention, which requires knowing "which entity
   submitted this order" to prevent the same entity from trading
   against itself. If we wait until Phase 8 to invent a client
   identity concept, Phase 5's connection model would need retrofitting.

## Decision

### ClientId as a strong wrapper

Introduce `ClientId` as a strong-typed wrapper struct in `core/Types.hpp`:

```cpp
struct ClientId {
    uint64_t value;
    explicit constexpr ClientId(uint64_t v) : value(v) {}
    constexpr bool operator==(const ClientId&) const = default;
    constexpr bool operator!=(const ClientId&) const = default;
};
// + std::hash specialization
```

Properties:
- **Strong-typed:** Same pattern as `OrderId`, `Price`, `Quantity` — explicit
  constructor prevents `ClientId` and `OrderId` from being accidentally
  interchanged (both are `uint64_t` underneath).
- **Ephemeral:** Assigned at TCP connection accept time from a monotonically
  increasing counter. Not persisted across reconnects. No session/auth concept.
- **Lives in `core/Types.hpp`:** Even though Phase 5 is the first *user*,
  it's a fundamental type consumed by Phase 8 (risk) and Phase 9 (FIX).
  Placing it in `core/` follows the same principle as `OrderId` (used by
  multiple layers, not owned by any one).

### Queue payloads: TaggedCommand / TaggedResponse

The inbound SPSC queue carries `TaggedCommand{ClientId, EngineCommand}` and
the outbound carries `TaggedResponse{ClientId, EngineResponse}`. The
`ClientId` tag is what enables response routing: the I/O thread looks up
`client_to_fd_[resp.client]` to find the socket to write to.

### Outbound queue sizing: 65536 slots

The outbound `SpscRingBuffer<TaggedResponse, 65536>` is sized generously
because:
- A single aggressive order can produce multiple responses (one per fill in
  a multi-level sweep) — the burstiest direction
- The engine must never silently drop a response (unlike inbound, where a
  full queue drops a client command — recoverable by the client re-sending)
- 65536 is a power of two (required by the ring buffer for bitwise modulo)
- At ~80 bytes per `TaggedResponse`, this is ~5MB — negligible on a server

### R8 back-pressure: engine spin-retries on outbound full

When the outbound queue is full:
- The engine thread spin-yields until space is available
- The queue capacity is large enough (65536) that this path is unreachable
  under normal operation
- The accepted pathological case: a client that never reads (so outbound
  backs up) can stall the engine thread. This mirrors the bounded-memory
  priority of Phase 4 (inbound: reject) while preserving request/response
  semantics (responses are never dropped silently)

## Alternatives Considered

1. **Bare `using ClientId = uint64_t;` type alias:**
   - Allows `ClientId` and `OrderId` to silently interchange — a function
     taking `(ClientId, OrderId)` could be called with `(OrderId, ClientId)`
     without compiler error.
   - Rejected: the strong wrapper catches this at compile time.

2. **File-descriptor (fd) as client identity:**
   - Simpler — no separate counter needed, the OS provides it.
   - Rejected: fds are recycled. When client A disconnects and client B
     connects, B might get the same fd. Response routing via stale fds would
     deliver A's pending responses to B. A monotonic counter avoids this.

3. **Session-based identity (UUID, login token):**
   - Would support reconnection ("I was client X, reconnect me to my previous
     session").
   - Rejected as over-engineering: this project has no authentication or session
     persistence requirements. Ephemeral per-connection is sufficient.

4. **Waiting until Phase 8 to introduce ClientId:**
   - Phase 8's risk engine needs "which entity submitted this" anyway.
   - Rejected because Phase 5's routing mechanism already needs per-connection
     identity. Introducing it here avoids retrofitting in Phase 8.

5. **Outbound queue: drop responses on full (same as inbound):**
   - Would prevent engine stall but silently lose responses.
   - Rejected: a client that sent a command and never gets a response is worse
     than a briefly stalled engine. Inbound drops are recoverable (client
     can retry); outbound drops are not (client doesn't know the engine
     processed its order).

6. **Outbound queue: smaller capacity (e.g., 4096, same as inbound):**
   - Saves memory (~5MB → ~320KB).
   - Rejected because the outbound is burstier (fills can produce N responses
     from one command), and the spin-retry penalty is more severe (stalls the
     engine). Over-provisioning the outbound is cheap insurance.

## Consequences

- **Positive:** Type-safe response routing from day one. No retrofitting needed
  in Phase 8/9. Strong wrapper prevents ClientId/OrderId confusion at compile
  time. Queue sizing eliminates outbound exhaustion under normal load.
- **Negative:** 8 bytes per `TaggedCommand`/`TaggedResponse` for the ClientId
  field. Ephemeral IDs mean no session continuity across reconnects — acceptable
  for this project's scope.
- **Enforced by:** `ClientId` explicit constructor prevents implicit conversions.
  Queue capacity is a compile-time template parameter. R8 spin-retry is in the
  engine thread loop (`apps/exchange_server/main.cpp`).
