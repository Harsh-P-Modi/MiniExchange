# ADR-005: Client-Supplied Lifetime-Unique Order IDs

**Status:** Accepted  
**Date:** Phase 1 (documented Phase 5)

## Context

Every order needs a unique identifier for:
- **Cancel:** the client says "cancel order X" — the engine must find
  X in O(1).
- **Duplicate detection:** submitting the same ID twice is a business
  error, not a crash.
- **Response correlation:** the client needs to know which order a
  fill/ack refers to.

The choice: who generates the ID, and what guarantees does it carry?

Real-world FIX gateways use client-supplied `ClOrdID` — the client
picks the ID, the exchange validates uniqueness. This mirrors
production behavior and pushes ID-generation complexity to the client
(where it belongs — the client knows its own numbering scheme), while
keeping the engine's job simple: validate and reject duplicates.

## Decision

`OrderId` is client-supplied. The engine validates uniqueness against
an `std::unordered_map<OrderId, Order*>` — O(1) amortized lookup for
both duplicate detection on submit and order location on cancel.

The uniqueness invariant is **lifetime-global**: once an `OrderId` is
used (whether the order fills, rests, or is cancelled), that ID can
never be reused. This is deliberate — it prevents subtle bugs where a
late-arriving cancel targets a different order that reused the same ID.

```cpp
// core/Types.hpp
struct OrderId {
    uint64_t value;
    explicit constexpr OrderId(uint64_t v) : value(v) {}
    // == , != , std::hash specialization
};
```

On duplicate: the engine returns
`EngineResult::DuplicateOrderId` — a structured result, not an
exception. No EventSink notification is emitted for rejections (only
successful state changes produce events).

## Alternatives Considered

1. **Server-generated auto-increment** — the engine assigns
   `next_id++` to each accepted order and returns it to the client.
   Rejected:
   - Doesn't match FIX/production semantics (client must track its own
     IDs regardless, since it needs to cancel by ID before the server
     response arrives in a pipelined protocol).
   - Adds a round-trip dependency: client cannot send order + immediate
     cancel-if-not-filled in a pipeline without waiting for the server
     to assign the ID first.
   - Requires the engine to communicate the assigned ID back to the
     client — more response fields, more complexity for no gain.

2. **UUID (128-bit random)** — guarantees uniqueness without
   coordination. Rejected:
   - 16 bytes vs. 8 bytes per order — doubles the ID storage cost in
     every `Order` struct, every `Trade` report, every hash-map key.
   - Hashing 128 bits is slower than hashing 64 bits.
   - Uniqueness is already guaranteed by the engine's duplicate check;
     the probabilistic uniqueness of UUIDs is solving a problem that
     doesn't exist here (there's only one engine instance, not a
     distributed system needing coordination-free ID generation).

3. **Client-supplied but with server-enforced sequential ordering**
   (IDs must arrive in strict ascending order) — allows gap detection
   and replay. Rejected: adds a per-client sequence state the engine
   must track, complicates multi-client scenarios (each client has its
   own counter), and doesn't match the project's goals (no
   reliability/replay guarantees needed for a portfolio piece).

4. **Weak uniqueness (only unique among currently-resting orders)**
   — IDs are recycled once an order leaves the book. Rejected: creates
   an ABA problem where a late cancel packet (in a networked
   environment) could target the wrong order. Lifetime uniqueness is
   the safe default; reclamation can be added later if memory pressure
   requires it (it won't — `uint64_t` gives 18 quintillion IDs).

## Consequences

- **Positive:** O(1) amortized cancel and duplicate detection via
  `unordered_map<OrderId, Order*>`. Mirrors real FIX gateway semantics
  (useful for Phase 9). No internal ID counter needed — engine is
  stateless with respect to ID generation. Clients can pipeline
  operations without waiting for server-assigned IDs.
- **Negative:** Clients must ensure uniqueness themselves. A buggy
  client sending duplicate IDs gets `DuplicateOrderId` rejections.
  Lifetime-unique constraint means the engine's `unordered_map` grows
  monotonically (entries for filled/cancelled orders remain as
  tombstones). Acceptable for a single-symbol portfolio project;
  production systems would add periodic ID-space compaction or
  per-session scoping.
- **Enforced by:** `MatchingEngine::submit()` checks the map before
  any book insertion. Unit tests explicitly verify duplicate rejection.
  The `OrderId` strong type (not a bare `uint64_t`) prevents accidental
  use of a raw integer where an `OrderId` is expected.
