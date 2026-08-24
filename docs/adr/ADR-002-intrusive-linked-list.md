# ADR-002: Intrusive Doubly-Linked List for Per-Level Order Queues

**Status:** Accepted  
**Date:** Phase 1

## Context

Each price level in the order book maintains a FIFO queue of resting
orders. The queue must support:
- O(1) append (new order arrives at this price)
- O(1) removal of any node by pointer (cancel or full fill)
- O(1) front access (next order to match)
- No heap allocation beyond the order itself

The standard library offers `std::list<Order>`, `std::deque<Order>`, and
`std::vector<Order>` as candidates for this queue.

## Decision

Embed `prev`/`next` pointers directly in the `Order` struct (intrusive
doubly-linked list). `PriceLevel` maintains `head_` and `tail_` pointers
and performs all linking/unlinking. No separate node allocations exist.

```cpp
struct Order {
    // ... business fields ...
    Order* prev = nullptr;
    Order* next = nullptr;
    PriceLevel* level = nullptr;  // back-pointer for O(1) cancel
};
```

Ownership of the `Order` object lives in
`unordered_map<OrderId, unique_ptr<Order>>`. The intrusive list holds
raw, non-owning pointers into those same objects.

## Alternatives Considered

1. **`std::list<Order>`** — O(1) insert/remove, but allocates a
   separate list node per element (two allocations per order: the Order
   + the node). Defeats Phase 3's memory pool (pool would need to
   manage two different object sizes). Cache-unfriendly: traversal
   chases node → data → next node → data. Rejected per steering file
   hard rule and performance rationale.

2. **`std::deque<Order>`** — O(1) push_back, but O(n) arbitrary
   removal (must shift elements). Cancel would be O(n) within a level.
   Rejected: violates O(1) cancel requirement.

3. **`std::vector<Order>` with swap-and-pop** — O(1) removal, but
   destroys FIFO ordering (the swapped element jumps position).
   Rejected: violates price-time priority within a level.

4. **`std::vector<Order*>` with erase** — O(n) erase (shifts
   pointers). Same problem as deque for cancel. Rejected.

5. **Non-intrusive doubly-linked list (custom, separate node type)**
   — same O(1) operations as intrusive, but still requires a separate
   node allocation. Rejected: gains nothing over intrusive and doubles
   allocation/deallocation work.

## Consequences

- **Positive:** Single allocation per order. O(1) append, remove, and
  front access. Phase 3's memory pool manages one object type (`Order`),
  not two. Cache-friendly traversal (data is the node; no extra
  indirection).
- **Negative:** An `Order` cannot belong to multiple lists
  simultaneously (one set of prev/next). Acceptable — an order belongs
  to exactly one price level. Adds 24 bytes to `Order` (prev + next +
  level pointers). Manual pointer fixup in `PriceLevel::push_back` and
  `PriceLevel::remove` (contained to one file, tested exhaustively).
- **Enforced by:** `.kiro/steering/tech.md` hard rule ("No
  `std::list`"). `PriceLevel` unit tests verify FIFO and correct
  relinking after arbitrary removal.
