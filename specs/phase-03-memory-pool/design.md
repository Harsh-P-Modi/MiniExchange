# Phase 3 — Design: Memory Pool

Status: **APPROVED** — `tasks.md` is written from this version and the
phase is implemented and test-verified.

## 1. Overview

Replace `OrderBook`'s `unordered_map<OrderId, unique_ptr<Order>>` as
the *owner* of `Order` lifetime with a fixed-capacity `OrderPool`. Per
Phase 1 `design.md` §8's explicit prediction, this swap is contained
entirely to "who owns the `Order`" — `orderbook/`'s traversal logic and
`engine/`'s matching logic don't change at all, since both only ever
touch raw `Order*`.

## 2. `orderbook/OrderPool.hpp`

```cpp
class OrderPool {
public:
    explicit OrderPool(size_t capacity);   // R1: default 1,000,000,
                                            // caller-configurable

    // Returns nullptr if the pool is exhausted (R4) — caller (engine)
    // translates that into EngineResult::PoolExhausted, the pool
    // itself has no concept of EngineResult (orderbook/ doesn't know
    // about engine-level result types, per the existing dependency
    // direction).
    Order* acquire();

    // Returns a previously-acquired Order* to the pool. Undefined
    // behavior if called twice on the same pointer or with a pointer
    // this pool didn't allocate — caller (OrderBook) is responsible
    // for calling this exactly once per order, exactly when it's
    // actually done with it (fully filled or cancelled).
    void release(Order* order);

    size_t capacity() const { return capacity_; }
    size_t available() const;   // for tests/diagnostics; O(1)

private:
    // Fixed-size backing storage — never reallocates, so every Order*
    // handed out remains valid (and at a stable address) for the
    // pool's entire lifetime. This is what makes it safe for Order's
    // prev/next/level intrusive pointers (Phase 1) to keep working
    // completely unchanged.
    std::unique_ptr<Order[]> storage_;
    size_t capacity_;

    // Intrusive free list: an unused slot's own memory stores the
    // index of the next free slot, reusing Order's first 8 bytes for
    // this purpose while the slot is free (a freed Order isn't a
    // valid Order — this is safe precisely because no one holds a
    // pointer to it anymore once release() has been called).
    // Represented as an index rather than a raw next-free pointer for
    // one concrete reason: it lets acquire()/release() validate "is
    // this index in range" trivially in debug builds, which a bare
    // pointer can't self-check.
    size_t free_list_head_;   // index into storage_, or `capacity_`
                              // as the sentinel for "list is empty"
    size_t free_count_;
};
```

## 3. `OrderBook` change

`OrderBook::orders_` changes from `unordered_map<OrderId,
unique_ptr<Order>>` to `unordered_map<OrderId, Order*>` (no longer
owning — just an index for O(1) lookup, same as before) plus a new
`OrderPool pool_` member that *is* the owner now. `insert()` calls
`pool_.acquire()` instead of `std::make_unique<Order>`; `remove()`
calls `pool_.release(order)` instead of letting a `unique_ptr` go out
of scope.

This is the entire diff to `OrderBook` — no change to `PriceLevel`, no
change to `MatchingEngine`'s matching logic, no change to any test that
doesn't specifically test pool exhaustion or allocation behavior.

## 4. `EngineResult::PoolExhausted` (R4)

New enum value added to `core/Events.hpp`'s `EngineResult` (Phase 1).
`MatchingEngine::submit` checks `OrderBook::insert`'s return; if it's
`nullptr` (pool exhausted), return `EngineResponse{PoolExhausted, {},
requested_qty}` without having mutated any other state — same "no side
effects on rejection" discipline as every other rejection path from
Phase 1.

## 5. Complexity vs. Charter/Phase 1 targets

| Operation | This design | Target |
|---|---|---|
| Acquire (new order) | O(1) — free-list pop | O(1), replaces prior heap allocation |
| Release (fill/cancel) | O(1) — free-list push | O(1), replaces prior heap deallocation |
| Pool exhaustion check | O(1) — `free_list_head_ == capacity_` | O(1) |

## 6. Judgment calls made here — flag if any should change

1. **Index-based free list, not pointer-based.** A raw "next free
   `Order*`" would work too and is marginally simpler, but an index
   allows a cheap bounds-check assertion in debug builds
   (`assert(free_list_head_ < capacity_)`) that a bare pointer doesn't
   support as naturally. Small win, worth it given the Charter's
   emphasis on invariants-as-assertions.
2. **`std::unique_ptr<Order[]>` for backing storage**, not a raw
   `new Order[capacity]` or a `std::vector<Order>`. `unique_ptr<Order[]>`
   gives array-delete semantics with RAII cleanup and no accidental
   resize/reallocate risk that `std::vector` would technically allow if
   someone later called `push_back` on it by mistake — `vector` being
   *capable* of reallocating, even if this code never triggers it, is a
   footgun the pool's whole purpose is to eliminate.
3. **`acquire()` doesn't construct/initialize the returned `Order`
   beyond whatever `Order[]`'s default array-initialization gives it**
   — the caller (`OrderBook::insert`) is responsible for setting all
   fields on the freshly-acquired slot before linking it into a
   `PriceLevel`. Worth confirming you're fine with "acquire returns a
   blank slot, caller fills it in" rather than `acquire(Order data)`
   taking the full order data and constructing in place — the latter
   is arguably cleaner API-wise but couples `OrderPool` to knowing
   `Order`'s full field set for construction, which `OrderPool`
   otherwise doesn't need to know beyond `sizeof(Order)`.

---

Once approved, `tasks.md` breaks this into steps: `OrderPool` first (in
isolation, tested standalone), then the `OrderBook` ownership swap,
then `PoolExhausted` wiring through `MatchingEngine`, then re-running
Phase 1's full test suite unchanged (to prove the swap really is
invisible), then the Phase 2 vs. Phase 3 benchmark comparison.
