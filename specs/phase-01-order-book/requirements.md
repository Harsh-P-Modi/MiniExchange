# Phase 1 — Requirements: Limit Order Book + Matching Engine

Status: **APPROVED** — resolved through iterative review (OrderId
lifetime-uniqueness, ownership model, `Fill`→`Trade` unification,
`NewOrder` variant, ports/events). `design.md` and `tasks.md` are built
on this version.

## 1. Scope

A single-symbol, single-threaded, in-process matching engine with:
Limit orders, Market orders, Cancel. Strict price-time priority. No
network I/O, no persistence, no risk checks (Phase 8), no multi-symbol
routing (later). A CLI app (`apps/cli/`) drives the engine for manual
testing; GoogleTest drives it for automated correctness testing.

Out of scope for Phase 1 (explicitly deferred):
- IOC / FOK / Post-Only / Iceberg / Stop / Cancel-Replace (later order types)
- Self-trade prevention (Phase 8 — risk engine)
- Tick-size / price-band validation (Phase 8)
- Multi-symbol support (orchestration layer, later)
- Any concurrency (Phase 4)
- Any allocation optimization (Phase 3) — correctness first, `std::map`
  and normal ownership are fine here

## 2. Core Types (core/)

- `OrderId` — unsigned 64-bit integer, client-supplied.
- `Price` — signed 64-bit integer, ticks. No implicit conversion to/from
  floating point anywhere in the engine.
- `Quantity` — unsigned 64-bit integer.
- `Side` — enum `{ Buy, Sell }`.
- `Sequence` — monotonically increasing 64-bit counter, assigned by the
  engine at insertion time, used solely for FIFO tiebreaking. Not
  client-visible as an input.
- `TradeSequence` — a **separate** monotonically increasing 64-bit
  counter (same underlying type as `Sequence`, distinct instance),
  assigned per executed trade. Kept independent from `Sequence` because
  trade numbering (needed by Phase 6's market-data feed and by replay)
  and order-arrival numbering (needed for FIFO) are different concerns
  that happen to both be monotonic counters — conflating them would
  make a future change to one implicitly affect the other.
- `Trade` — `{ trade_sequence: TradeSequence, buy_order_id: OrderId,
  sell_order_id: OrderId, price: Price, quantity: Quantity }`. One
  canonical type, used both inside `EngineResponse` (§4) and as the
  `EventSink::on_trade` payload (§3) — not two near-duplicate structs.
  Buy/sell is the canonical representation everywhere `Trade` appears —
  CLI output, and later UDP feed/FIX/replay — none of which have a
  concept of "incoming vs resting" (that's purely a matching-mechanics
  artifact, not a property of the trade). The CLI may cosmetically
  annotate "(yours)" next to whichever ID matches the order just
  submitted; that's `apps/cli/` presentation logic, not a `Trade` field.

## 2.2 `NewOrder` (input to `EngineAPI::submit`)

```cpp
struct LimitOrder {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};

struct MarketOrder {
    OrderId id;
    Side side;
    Quantity quantity;   // no price field — structurally impossible
                          // to attach one, not just a runtime rule
};

using NewOrder = std::variant<LimitOrder, MarketOrder>;
```

No separate `OrderType` enum is needed — the variant's alternative
(`LimitOrder` vs `MarketOrder`) *is* the type, checked via
`std::holds_alternative`/`std::visit` rather than compared against an
enum value. Note also that the resting `Order` type inside `orderbook/`
(defined in `design.md`) only ever represents limit-origin state — a
`MarketOrder` never rests (R10), so there's no ambiguity about what a
resting order's "type" could be.

Chosen over a single flat struct with an ignored `price` field: making
"a Market order with a price" compile-time-unrepresentable is worth the
`std::visit` boilerplate at the one call site (`submit`) that needs it.
This also simplifies R4/R11 below — "Market orders carry no price"
becomes a fact of the type system, not a rule the engine has to check
at runtime.

## 2.1 Order Lifetime & Ownership (resolved)

- **OrderIds are lifetime-unique**, not merely resting-unique: once an
  `OrderId` has ever been accepted by the engine, it can never be
  reused for the lifetime of the engine instance — even after the
  order fully fills or is cancelled. The engine maintains an
  `unordered_set<OrderId>` of every ID ever accepted, separate from the
  resting-orders index, specifically to detect reuse. (This resolves a
  contradiction in the earlier draft: R2 said "currently resting," the
  `EngineResult` comment said "or already used" — lifetime-unique is
  correct and matches real gateway behavior.) Unbounded growth of this
  set is acceptable in Phase 1 per the existing "no allocation
  optimization yet" scope. Unlike pooling resting `Order` objects
  (which Phase 3 does address), this set's unbounded growth is **not
  assigned to any phase in this project's scope** — it never shrinks,
  by design, since IDs are never reused, and no Phase 1–10 requirement
  currently commits to bounding it. That's an accepted permanent
  characteristic of this project (consistent with the Charter's
  non-goal of being a production system with unbounded runtime), not a
  dropped task — worth naming explicitly as a "what I'd do differently
  for production" talking point rather than treating it as unfinished
  work.
- **Ownership**: `unordered_map<OrderId, std::unique_ptr<Order>>` is the
  sole owner of every resting `Order`'s lifetime. The intrusive
  per-price-level doubly-linked list holds raw, non-owning `Order*`
  (the `prev`/`next` pointers live inside `Order` itself, per the
  earlier intrusive-list decision). `orderbook/` and `engine/` never
  see or manage ownership — they only traverse raw pointers. This keeps
  the Phase 3 swap (map → memory pool) contained entirely to *who owns*
  the `Order`, with zero changes to any traversal or matching code.

## 3. Ports (interfaces/)

Two ports, per the locked architecture in steering/`structure.md`:

**Input port — `EngineAPI`** (implemented by `MatchingEngine`):
```cpp
class EngineAPI {
public:
    virtual EngineResponse submit(const NewOrder&) = 0;
    virtual EngineResponse cancel(OrderId) = 0;
    virtual const OrderBook& book() const = 0;
    virtual ~EngineAPI() = default;
};
```

**Output port — `EventSink`** (implemented by adapters that care about
every state change, not just their own calls' results):
```cpp
class EventSink {
public:
    virtual void on_order_accepted(const OrderAccepted&) {}
    virtual void on_trade(const Trade&) {}
    virtual void on_order_cancelled(const OrderCancelled&) {}
    virtual ~EventSink() = default;
};
```
Default no-op bodies so an adapter only overrides what it cares about.
The engine holds an `EventSink*` via constructor injection; a
`NullEventSink` singleton is the default when nothing is wired up (e.g.
in unit tests that only check the returned `EngineResponse`).

## 4. Engine Result Contract (resolves the earlier open question)

`EngineResponse` is the richer struct — settled:

```cpp
enum class EngineResult {
    Accepted,          // order accepted (may have filled, partially or fully)
    DuplicateOrderId,  // ADD with an OrderId ever previously accepted (lifetime-unique, see §2.1)
    UnknownOrderId,    // CANCEL referencing an OrderId not currently resting
    InvalidQuantity,   // qty == 0
    InvalidPrice,      // price <= 0 (limit orders only; market orders carry no price)
};

struct EngineResponse {
    EngineResult status;
    std::vector<Trade> trades;   // empty if no match occurred
    Quantity remaining_qty;      // 0 if fully filled or fully cancelled
};
```

Rationale for keeping both `EngineResponse` (synchronous, per-caller)
*and* `EventSink` (async-style, broadcast) rather than just one: the
caller of `submit()` needs "what happened to *my* order" immediately
and without needing to have registered a listener beforehand — that's
`EngineResponse`. A market-data adapter (Phase 6) or a benchmark counter
needs "what happened to *any* order," including trades triggered by
someone else's cancel-then-repost — that's what `EventSink` is for.
Collapsing these into one mechanism would force either the caller to
poll a global event log, or every adapter to implement a full
`EventSink` just to get its own order's result — both worse than having
two small, single-purpose channels.

## 5. Functional Requirements (EARS)

**Order submission — Limit**
- R1: WHEN the engine receives an `ADD` request carrying a `LimitOrder`
  (§2.2) whose `OrderId` has never previously been accepted by the
  engine (see §2.1), THE ENGINE SHALL insert the order into the book at
  its price level, ordered after all existing orders at that price
  level (FIFO), THEN attempt to match it per R5–R8.
- R2: WHEN an `ADD` request references an `OrderId` that has ever
  previously been accepted by the engine (whether currently resting,
  fully filled, or cancelled — see §2.1, lifetime-unique), THE ENGINE
  SHALL reject it with `DuplicateOrderId` and SHALL NOT modify book
  state.
- R3: WHEN an `ADD` request (either `LimitOrder` or `MarketOrder`, per
  §2.2) has `Quantity = 0`, THE ENGINE SHALL reject it with
  `InvalidQuantity` and SHALL NOT modify book state.
- R4: WHEN an `ADD` (`LimitOrder`) request has `Price <= 0`, THE ENGINE
  SHALL reject it with `InvalidPrice` and SHALL NOT modify book state.
  (This is the only remaining runtime check for price — a `MarketOrder`
  cannot carry a price at all, per §2.2, so no equivalent check applies
  to it.)

**Matching**
- R5: WHEN an incoming Buy order's price is greater than or equal to the
  best resting Sell price (or vice versa for Sell), THE ENGINE SHALL
  match against resting orders starting at the best price level,
  consuming resting orders in FIFO order within that level, until
  either the incoming order is fully filled or no further crossing
  price level exists.
- R6: WHEN a match occurs, THE ENGINE SHALL execute the trade at the
  **resting order's price** (not the incoming order's price).
- R7: WHEN a resting order is fully consumed by a match, THE ENGINE
  SHALL remove it from the book and from the O(1) cancel-lookup index.
- R8: WHEN an incoming Limit order is partially filled and crossing
  liquidity is exhausted, THE ENGINE SHALL rest the remaining quantity
  on the book at its limit price, in FIFO position at that price level.

**Order submission — Market**
- R9: WHEN the engine receives an `ADD` request carrying a
  `MarketOrder` (§2.2), THE ENGINE SHALL match immediately against
  resting orders on the opposite side starting at the best available
  price, without a limit price constraint (structurally guaranteed by
  `MarketOrder` having no price field), consuming price levels in
  best-to-worst order.
- R10: WHEN a Market order's quantity exceeds total available opposite-
  side liquidity, THE ENGINE SHALL fill what it can and SHALL discard
  the unfilled remainder — a Market order never rests on the book.
- R11: A `MarketOrder` cannot carry a price — enforced structurally by
  `NewOrder`'s variant shape (§2.2), not a runtime check. `InvalidPrice`
  therefore never applies to a `MarketOrder`. `InvalidQuantity`
  (R3-equivalent) still applies to `MarketOrder.quantity`.

**Cancel**
- R12: WHEN the engine receives a `CANCEL` request for an `OrderId`
  currently resting on the book, THE ENGINE SHALL remove that order
  from its price level and from the cancel-lookup index in O(1) time
  and return `Accepted`.
- R13: WHEN the engine receives a `CANCEL` request for an `OrderId` not
  currently resting (never existed, already filled, or already
  cancelled), THE ENGINE SHALL return `UnknownOrderId` and SHALL NOT
  modify book state.

**Self-crossing**
- R14: WHEN an incoming order would cross against a resting order
  (regardless of whether they originated from the "same" CLI session —
  the engine has no concept of client identity in Phase 1), THE ENGINE
  SHALL match them normally. No self-trade prevention in Phase 1.

**Book introspection**
- R15: THE ENGINE SHALL expose a read-only view of the book (price
  levels, aggregate quantity per level, and per-level order queues)
  sufficient for a `PRINT_BOOK` command to render top-of-book and full
  depth without the CLI reaching into engine internals.

**Event emission (EventSink)**
- R16: WHEN an `ADD` request is accepted (per R1/R9), THE ENGINE SHALL
  call `EventSink::on_order_accepted` exactly once, in addition to
  returning `EngineResponse` to the caller.
- R17: WHEN a match occurs (per R5–R6, R9), THE ENGINE SHALL call
  `EventSink::on_trade` once per individual trade (a single incoming
  order crossing three resting orders emits three `Trade` events, not
  one aggregated event) — `EngineResponse.trades` returned to the
  caller aggregates the same trades for convenience, but `EventSink`
  always sees them individually.
- R18: WHEN a resting order is cancelled (per R12), THE ENGINE SHALL
  call `EventSink::on_order_cancelled` exactly once.
- R19: Rejections (`DuplicateOrderId`, `UnknownOrderId`,
  `InvalidQuantity`, `InvalidPrice`) SHALL NOT trigger any `EventSink`
  call — only state changes that actually happened are events.
- R20: `EventSink` calls SHALL occur synchronously, in the same call
  stack as `submit`/`cancel`, before `EngineResponse` is returned to the
  caller (no separate thread, no queue, in Phase 1 — that nuance is a
  Phase 4 concern once TCP/UDP adapters run on different threads).

## 6. Non-Functional Requirements

- NFR1: The engine SHALL NOT perform any I/O (stdout, stderr, files,
  sockets, logging) under any code path.
- NFR2: The engine SHALL NOT depend on wall-clock time; all ordering is
  via the monotonic `Sequence` counter.
- NFR3: The engine SHALL be single-threaded and SHALL NOT be
  responsible for its own thread-safety in Phase 1 (Phase 4 concern).
- NFR4: `Cancel` lookup, resting-order lookup, and the lifetime-unique
  `OrderId` check (§2.1) SHALL each be O(1) amortized (hash-based
  lookups), independent of book depth or total IDs ever seen.
- NFR5: No floating-point type SHALL appear anywhere in `core/`,
  `orderbook/`, or `engine/`.

## 7. CLI App Contract (apps/cli/) — presentation only, no logic

`apps/cli/` is an executable (composition root): `main.cpp` wires
`CLIParser → EngineAPI → ConsolePrinter` directly. There is no separate
`adapters/cli/` library in Phase 1 — the parser/printer aren't reused
anywhere else yet. If a second executable later needs the same parsing
logic, that's the signal to extract an `adapters/` library, not before.

Commands (whitespace-delimited, one per line):
```
ADD <id> BUY <price> <qty>
ADD <id> SELL <price> <qty>
MARKET <id> BUY <qty>
MARKET <id> SELL <qty>
CANCEL <id>
PRINT_BOOK
```
The CLI SHALL translate each line into an `EngineAPI` call, then render
the returned `EngineResponse`/`EngineResult` as human-readable text
(e.g. `REJECTED: DuplicateOrderId`, `FILLED 10@10020, RESTING 5@10020`,
`OK: cancelled 42`). All formatting logic lives in `apps/cli/`, never in
`engine/`.

The CLI does **not** need to implement `EventSink` in Phase 1 — with a
single app and no concurrent consumers, `EngineResponse` alone is
sufficient for its needs. `EventSink` exists as a port from Phase 1
onward specifically so Phase 6 (UDP feed) and Phase 2 (benchmark app)
can subscribe later without any change to `engine/`. Tests
may implement a trivial `RecordingEventSink` to assert R16–R20 directly.

## 8. Test Requirements

- Unit tests (GoogleTest) for every EARS requirement above, including
  edge cases: cancel-after-fill, cancel-after-cancel, zero-quantity,
  negative price, market order against empty book, market order partial
  fill, multiple price levels, FIFO ordering within a level, order that
  crosses multiple price levels in one match.
- Include tests using a `RecordingEventSink` to verify R16–R20 (correct
  event count/order/content), independent of `EngineResponse` tests.
- No performance assertions in Phase 1 (that's Phase 2).

---

**Approval needed on:** everything above, as written — this revision
resolves the `OrderId` uniqueness contradiction (§2.1), settles Order
ownership (§2.1), renames `Fill`→`Trade` throughout (§2, §4), and
replaces the flat `OrderType` enum with a `std::variant<LimitOrder,
MarketOrder>` `NewOrder` (§2.2) so "Market order with a price" is
compile-time-impossible rather than a runtime rule.

Once approved, next is `design.md` (concrete class layout: `Order`,
`PriceLevel`, intrusive list node layout, `OrderBook`, `MatchingEngine`,
the `unordered_map<OrderId, unique_ptr<Order>>` index, and the separate
`unordered_set<OrderId>` for lifetime-uniqueness).
