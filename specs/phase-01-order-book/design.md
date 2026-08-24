# Phase 1 — Design: Limit Order Book + Matching Engine

Status: **APPROVED** — `tasks.md` is written from this version.

This document turns `requirements.md` into concrete types and class
shapes. Where I made a judgment call rather than something already
locked by `requirements.md`/steering, I've flagged it explicitly at the
end (§8) — everything else here is a direct consequence of decisions
already approved.

## 1. `core/Order.hpp` — the resting order + intrusive node

```cpp
struct Order {
    OrderId   id;
    Side      side;
    Price     price;         // the order's limit price (a Market order
                              // never reaches this struct — see §5)
    Quantity  quantity;       // remaining, unfilled quantity
    Sequence  sequence;       // insertion order, for FIFO tiebreak

    // Intrusive doubly-linked list pointers — this IS the queue.
    // No separate node type, no heap allocation beyond the Order
    // itself (locked in tech.md: "no std::list, ever").
    Order* prev = nullptr;
    Order* next = nullptr;

    // Back-pointer to the PriceLevel this order currently sits in.
    // Needed for O(1) cancel: given only an OrderId -> Order*, we must
    // be able to (a) unlink from the intrusive list using prev/next,
    // and (b) decrement that level's aggregate quantity and detect
    // "level now empty -> remove level from the tree" — without this
    // back-pointer we'd need to know the price separately and re-look
    // it up in the tree, turning O(1) cancel into O(log P).
    PriceLevel* level = nullptr;
};
```

`Order` has no methods — pure data, per `core/`'s rule (steering:
`structure.md`). All linking/unlinking logic lives in `orderbook/`.

## 2. `orderbook/PriceLevel.hpp`

```cpp
class PriceLevel {
public:
    explicit PriceLevel(Price price) : price_(price) {}

    void push_back(Order* order);      // O(1): append to FIFO tail
    void remove(Order* order);          // O(1): unlink using order->prev/next
    Order* front() const { return head_; }
    bool empty() const { return head_ == nullptr; }

    Price price() const { return price_; }
    Quantity total_quantity() const { return total_qty_; }

private:
    Price price_;
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
    Quantity total_qty_ = 0;   // maintained incrementally on push/remove,
                               // so "total resting qty at this level" is
                               // O(1) to read (needed for PRINT_BOOK and,
                               // later, market-data depth publishing)
};
```

`push_back`/`remove` update `total_qty_` and fix up `head_`/`tail_` and
the moved order's `prev`/`next`/`level` fields. This is where the
"hash map and intrusive list never disagree" invariant (charter) is
mechanically enforced — `remove` is the *only* code path that unlinks,
and it's always called through `OrderBook`, never directly.

## 3. `orderbook/OrderBook.hpp`

```cpp
class OrderBook {
public:
    // Ownership: this map is the sole owner of every resting Order's
    // lifetime (§2.1 of requirements.md). The intrusive list inside
    // each PriceLevel holds raw, non-owning Order* into these same
    // objects.
    using OrderOwnerMap = std::unordered_map<OrderId, std::unique_ptr<Order>>;

    // Bids sorted descending (best bid = highest price = begin()).
    // Asks sorted ascending (best ask = lowest price = begin()).
    // Two separate maps rather than one map with a side-conditional
    // comparator: simpler to reason about, and best_bid()/best_ask()
    // are each a plain begin() with no branching.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>>    asks_;
    OrderOwnerMap orders_;

    // Structural primitives engine/ composes into matching logic.
    // OrderBook does NOT decide whether to match anything — it only
    // knows how to insert, remove, and report the top of book.
    Order* insert(std::unique_ptr<Order> order);   // returns raw ptr for convenience
    bool remove(OrderId id);                        // O(1); false if unknown
    bool contains(OrderId id) const;                 // O(1)

    PriceLevel* best_bid();   // nullptr if bids_ empty
    PriceLevel* best_ask();   // nullptr if asks_ empty

    // Called by engine/ after a level's front order is fully consumed
    // during matching, or after the last order at a level is cancelled:
    // removes the now-empty level from the tree. O(log P).
    void erase_level_if_empty(Side side, Price price);
};
```

Why `Order* insert(...)` returns a raw pointer despite the map owning a
`unique_ptr`: callers (the engine) need to link the returned `Order*`
into the correct `PriceLevel`'s intrusive list immediately — returning
the raw pointer avoids a second lookup, and ownership stays exactly
where it was (in `orders_`).

## 4. `interfaces/EngineAPI.hpp`, `interfaces/EventSink.hpp`

As specified in `requirements.md` §3 — unchanged here, included for
completeness of the dependency picture: `engine/` implements
`EngineAPI`; `apps/cli/` depends on `EngineAPI` and may implement
`EventSink`.

## 5. `engine/MatchingEngine.hpp` — implements `EngineAPI`

```cpp
class MatchingEngine : public EngineAPI {
public:
    explicit MatchingEngine(EventSink* sink = &NullEventSink::instance());

    EngineResponse submit(const NewOrder&) override;
    EngineResponse cancel(OrderId) override;
    const OrderBook& book() const override { return book_; }

private:
    OrderBook book_;
    std::unordered_set<OrderId> ever_seen_ids_;  // lifetime-uniqueness
                                                   // (requirements.md §2.1)
                                                   // — deliberately NOT
                                                   // part of OrderBook:
                                                   // this is a business
                                                   // rule about the
                                                   // engine's accept
                                                   // policy, not a
                                                   // structural property
                                                   // of "what's resting."
    Sequence next_sequence_ = 0;
    TradeSequence next_trade_sequence_ = 0;
    EventSink* sink_;

    EngineResponse submit_limit(const LimitOrder&);
    EngineResponse submit_market(const MarketOrder&);
    // Shared matching loop used by both: consumes opposite-side
    // liquidity starting at best price, up to `limit_price` (nullopt
    // for Market — no ceiling/floor) or until `remaining` hits 0.
    std::vector<Trade> match_against_book(
        Side incoming_side, OrderId incoming_id,
        Quantity& remaining, std::optional<Price> limit_price);
};
```

`submit()` dispatches via `std::visit` on the `NewOrder` variant to
`submit_limit`/`submit_market` — this is the one call site that needs
`std::visit` boilerplate, per the §2.2 tradeoff already accepted.

**Matching loop shape** (`match_against_book`), the core algorithm:

```
remaining = incoming order's quantity
trades = []
while remaining > 0:
    level = opposite_side.best_level()
    if level is null: break                      // no liquidity left
    if limit_price is set and level crosses it is false: break   // R5
    while remaining > 0 and level not empty:
        resting = level.front()
        fill_qty = min(remaining, resting.quantity)
        trade = Trade{ next_trade_sequence_++,
                       buy_order_id  = (incoming_side==Buy ? incoming_id : resting.id),
                       sell_order_id = (incoming_side==Sell ? incoming_id : resting.id),
                       price = resting.price,      // R6: resting order's price
                       quantity = fill_qty }
        trades.push_back(trade)
        sink_->on_trade(trade)                     // R17: emitted per fill, immediately
        remaining -= fill_qty
        resting.quantity -= fill_qty
        if resting.quantity == 0:
            book_.remove(resting.id)                // R7
    if level.empty(): book_.erase_level_if_empty(...)
return trades
```

This same loop serves both R5–R8 (Limit) and R9–R10 (Market) — the
only difference is `limit_price` being present (Limit) or absent
(Market), and what the *caller* does with leftover `remaining` after
the loop returns (Limit rests it per R8; Market discards it per R10).
Keeping one shared loop rather than two near-duplicate ones is
deliberate: R5/R6/R7/R9/R10 are really "one matching algorithm with a
parameter," and duplicating it would risk the two copies drifting.

## 6. `apps/cli/` — sketch, not full design (thin by design)

```
main.cpp: constructs MatchingEngine (no EventSink needed, per
          requirements.md §7), reads lines from stdin, calls
          CLIParser::parse(line) -> NewOrder or CancelRequest or
          PrintBookRequest, dispatches to engine, calls
          ConsolePrinter::render(EngineResponse) or
          ConsolePrinter::render(const OrderBook&) for PRINT_BOOK.
```
Full CLI grammar/parsing details belong in `tasks.md`, not `design.md`
— there's no data-structure decision left to make here, just parsing
and formatting code.

## 7. Complexity vs. Charter Targets

| Operation | This design | Charter target |
|---|---|---|
| Cancel | O(1) — `orders_.erase` + `PriceLevel::remove` via prev/next | O(1) amortized ✓ |
| Duplicate/unknown ID check | O(1) — `unordered_set`/`unordered_map` lookup | O(1) amortized ✓ |
| Insert, new price level | O(log P) — `std::map::emplace` | O(log P) ✓ |
| Insert, existing price level | O(1) — `PriceLevel::push_back` | O(1) ✓ |
| Match | O(k) where k = number of resting orders consumed | O(number of fills) ✓ |
| Best bid / best ask | O(1) — `map::begin()` (amortized; technically O(log P) worst-case for `std::map`'s begin(), but practically O(1) since begin() is cached/iterator-stable in libstdc++'s red-black tree) | O(1) target |

Flagging that last row honestly rather than rounding it up: `std::map`
is a red-black tree; `begin()` itself is O(1) (leftmost node is cached),
so "best bid/ask" really is O(1) here. This stops being exactly true if
Phase 2/3 benchmarking replaces `std::map` with something else — that's
exactly the kind of thing Phase 2's comparison needs to re-verify, not
assume.

## 8. Judgment calls made here — flag if any should change

1. **Two separate `std::map`s (bids_/asks_)** rather than one map with
   a side-aware comparator. Simpler code, no behavioral difference.
2. **`erase_level_if_empty` as an explicit engine-called step** rather
   than `PriceLevel::remove` automatically triggering it. Keeps
   `PriceLevel` unaware of the tree it lives in (single responsibility)
   at the cost of the engine having to remember to call it — I think
   that's the right tradeoff since `orderbook/` shouldn't reach back
   into "which tree am I in," but worth you confirming.
3. **`Order::level` back-pointer** adds 8 bytes to `Order` to make
   cancel O(1) instead of O(log P). Given cancel-O(1) is an explicit
   charter/requirements target, this isn't really optional — flagging
   it anyway since it's the reason `Order` isn't a "pure value type."
4. **Shared `match_against_book` loop** for both Limit and Market
   rather than two separate matching functions — my strong preference,
   per the reasoning in §5, but it's a real design choice worth you
   seeing explicitly rather than just landing in the code.

---

Once approved, `tasks.md` breaks this into an ordered, checkable
implementation list (CMake skeleton, `core/` headers, `PriceLevel`,
`OrderBook`, `MatchingEngine`, `apps/cli/`, GoogleTest suite per every
R-number, GitHub Actions) — and that's what actually goes to Kiro.
