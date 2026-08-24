# Phase 1 — Tasks: Limit Order Book + Matching Engine

Status: **APPROVED PLAN — execute one task at a time**

**Execution rule (per `.kiro/steering/structure.md`): do exactly one
task below, then stop and wait for explicit review/approval before
starting the next one.** Do not batch multiple tasks in one
unattended run. Do not invent additional scope beyond what a task
describes. Do not start a task whose prerequisites (earlier tasks)
aren't complete and reviewed.

Each task lists: what to create/change, acceptance criteria (how to
verify it's actually done), and the specific `requirements.md`/
`design.md` sections it implements. After each task, also update
`docs/LEARNING.md` per `.kiro/steering/learning-doc.md` — this is not
optional and not a separate task, it's part of every task's
acceptance criteria below.

---

## Task 1 — Project skeleton & build system

Create:
- `CMakeLists.txt` (root) — C++20, `RelWithDebInfo` default, `-Wall
  -Wextra -Wpedantic` as errors, `FetchContent` for GoogleTest and
  Google Benchmark (per `.kiro/steering/tech.md`'s locked stack).
- `.gitignore` (ignore `build/`, IDE artifacts).
- `.clang-format` and `.clang-tidy` configs (Google C++ Style baseline,
  per `CHARTER.md`'s coding standards).
- Empty placeholder directories with `.gitkeep` (or a trivial header)
  for `core/`, `orderbook/`, `engine/`, `interfaces/`, `apps/cli/`,
  `tests/`, `docs/adr/` — these are Phase 1's own folders, populated by
  later tasks in *this* file. This does not contradict `PLAN.md`'s "no
  empty placeholder folders ahead of time" note — that note is about
  not pre-creating `adapters/*` or the other `apps/*` subfolders that
  belong to *later phases* (Phase 5+); it was never about Phase 1's own
  directories, which obviously need to exist for Phase 1 to write into.

**Acceptance criteria:** `cmake -S . -B build -G Ninja && cmake --build
build` succeeds with zero source files beyond the skeleton, and
`ctest --test-dir build` runs (even with 0 tests) without error.

---

## Task 2 — `core/Types.hpp`

Create `OrderId`, `Price`, `Quantity`, `Side` (enum `{Buy, Sell}`),
`Sequence`, `TradeSequence` per `requirements.md` §2. Use strong
type aliases or thin wrapper types (not bare `uint64_t`/`int64_t`
everywhere) — decide and document the choice in `docs/LEARNING.md`
(this is a real design decision: type-safety vs. simplicity).

**Acceptance criteria:** header compiles standalone; a `static_assert`
or short test confirms no implicit conversion from `Price` to
`Quantity` (or whatever type-safety mechanism was chosen) compiles
where it shouldn't.

**Implements:** `requirements.md` §2.

---

## Task 3 — `core/Trade.hpp`

Create the `Trade` struct: `trade_sequence`, `buy_order_id`,
`sell_order_id`, `price`, `quantity`, per `requirements.md` §2
(the resolved `Fill`→`Trade` unification).

**Acceptance criteria:** header compiles; a trivial test constructs a
`Trade` and reads back all fields correctly.

**Implements:** `requirements.md` §2.

---

## Task 4 — `core/Order.hpp` and `core/NewOrder.hpp`

Create:
- `Order` struct exactly as in `design.md` §1: `id`, `side`, `price`,
  `quantity`, `sequence`, intrusive `prev`/`next`, and the `level`
  back-pointer to `PriceLevel` (forward-declare `PriceLevel`).
- `LimitOrder`, `MarketOrder`, and `NewOrder = std::variant<LimitOrder,
  MarketOrder>` per `requirements.md` §2.2.

**Acceptance criteria:** headers compile; a test confirms
`std::holds_alternative<MarketOrder>` correctly distinguishes the two
variant alternatives, and confirms `MarketOrder` has no price member
(e.g. via `static_assert(!requires { std::declval<MarketOrder>().price;
})` or equivalent — pick a concrete mechanism and note which in
`docs/LEARNING.md`).

**Implements:** `requirements.md` §2, §2.2; `design.md` §1.

---

## Task 5 — `core/Events.hpp`

Create `EngineResult` (enum), `EngineResponse` struct (`status`,
`trades: vector<Trade>`, `remaining_qty`), and the event payload types
referenced by `EventSink` (`OrderAccepted`, `OrderCancelled` — define
their fields now; `Trade` from Task 3 doubles as the trade-event
payload per the earlier design decision).

**Acceptance criteria:** header compiles; a test constructs each type
and confirms field access.

**Implements:** `requirements.md` §4.

---

## Task 6 — `interfaces/EngineAPI.hpp` and `interfaces/EventSink.hpp`

Create the two port interfaces exactly as specified in
`requirements.md` §3, including `NullEventSink` (a singleton no-op
implementation of `EventSink`, used as the default when nothing is
wired up).

**Acceptance criteria:** headers compile; a test constructs a
`NullEventSink` and calls each method with a dummy payload, confirming
no crash/no-op behavior.

**Implements:** `requirements.md` §3.

---

## Task 7 — `orderbook/PriceLevel.hpp` / `.cpp`

Implement `PriceLevel` per `design.md` §2: `push_back`, `remove`,
`front`, `empty`, `price()`, `total_quantity()`. This is where the
intrusive-list linking/unlinking logic actually lives — `Order` itself
has no methods (Task 4), so all `prev`/`next`/`level` manipulation
happens here.

**Acceptance criteria (GoogleTest):**
- Push 3 orders, confirm FIFO order via repeated `front()`+manual
  advance (or an added test-only iteration helper).
- Remove the middle order, confirm the remaining two are still linked
  correctly (`prev`/`next` fixed up) and `total_quantity()` reflects
  the removal.
- Remove the only order, confirm `empty()` becomes true.
- Confirm `total_quantity()` is O(1) to read (this is a code-review
  check — no allocation/traversal in the getter — not something a unit
  test can directly measure, but flag it in review).

**Implements:** `design.md` §2.

---

## Task 8 — `orderbook/OrderBook.hpp` / `.cpp`

Implement `OrderBook` per `design.md` §3: `bids_`/`asks_` maps (correct
comparators — descending for bids, ascending for asks), `orders_`
(owning `unordered_map<OrderId, unique_ptr<Order>>`), `insert`,
`remove`, `contains`, `best_bid`, `best_ask`, `erase_level_if_empty`.

**Acceptance criteria (GoogleTest):**
- Insert orders at multiple price levels on both sides; confirm
  `best_bid()`/`best_ask()` return the correct level each time.
- Insert two orders at the same price; confirm FIFO ordering within
  that level (via `PriceLevel::front()`).
- `remove()` an order; confirm O(1)-shaped code path (no scan) and
  confirm the level is removed from the tree via
  `erase_level_if_empty` when it becomes empty.
- `remove()` an unknown `OrderId`; confirm `false` returned, no crash.
- `contains()` returns correct true/false for resting vs. non-resting
  IDs.

**Implements:** `design.md` §3; supports `requirements.md` R1, R7, R12,
R13, R15.

---

## Task 9 — `engine/MatchingEngine.hpp` / `.cpp`: construction, `submit_limit` (no matching yet)

Implement `MatchingEngine`'s constructor (pool-free for Phase 1 — just
`OrderBook` + `ever_seen_ids_` + sequence counters + `EventSink*`), and
`submit_limit` for the **non-crossing** case only: validate (R2, R3,
R4), insert into the book (R1), emit `on_order_accepted` (R16), return
`EngineResponse`. Do **not** implement matching yet — that's Task 10.
A limit order that *would* cross should, for this task only, still be
correctly rejected/accepted/inserted but matching logic comes next.

**Acceptance criteria (GoogleTest), directly traceable to
`requirements.md`:**
- R1: valid new limit order rests correctly.
- R2: duplicate `OrderId` (including previously-filled/cancelled —
  lifetime-unique per §2.1) rejected with `DuplicateOrderId`.
- R3: zero quantity rejected with `InvalidQuantity`.
- R4: non-positive price rejected with `InvalidPrice`.
- R16: `on_order_accepted` called exactly once on acceptance, zero
  times on any rejection (R19).

**Implements:** `requirements.md` R1–R4, R16, R19 (partial — no-match
path only); `design.md` §5.

---

## Task 10 — Matching loop: `match_against_book` + full `submit_limit`

Implement the shared matching loop from `design.md` §5, and wire it
into `submit_limit` for the crossing case: R5 (crossing detection), R6
(trade at resting price), R7 (fully-consumed resting orders removed),
R8 (partial fill rests remainder), R14 (no self-trade prevention —
match unconditionally), R17 (per-fill `on_trade` emission), R20
(synchronous emission before `EngineResponse` returns).

**Acceptance criteria (GoogleTest):**
- Incoming order crosses exactly one resting order, fully filling both
  → one `Trade`, both removed/resting-remainder-zero as appropriate.
- Incoming order crosses multiple resting orders across multiple price
  levels in one call → correct number of individual `Trade`s (via a
  `RecordingEventSink`), each at its *own* resting order's price (not
  the incoming order's price) — this is the concrete test for R6.
  R6.
- Incoming order partially fills against available liquidity, rests
  the remainder at its own limit price (R8).
- FIFO within a level is respected during matching (first-in order at
  a level fills before later ones at the same price).
- `RecordingEventSink` sees each fill as a separate `on_trade` call,
  while `EngineResponse.trades` aggregates the same fills.
- Self-crossing (submit an order that matches a resting order) matches
  normally, no special-casing (R14).

**Implements:** `requirements.md` R5–R8, R14, R17, R20; `design.md` §5.

---

## Task 11 — `submit_market`

Implement `submit_market`: uses the same `match_against_book` with no
`limit_price`, discards unfilled remainder (R10), never rests (R9).

**Acceptance criteria (GoogleTest):**
- Market order fully fills against available liquidity.
- Market order against empty book: no crash, zero fills, remainder
  discarded, `EngineResponse.remaining_qty` reflects the unfilled
  amount (R10).
- Market order partially fills, remainder confirmed **not** resting on
  the book afterward (query `book()` to confirm).
- Zero-quantity market order rejected with `InvalidQuantity` (R3
  applies per R11).
- Confirm (by construction, not a runtime check) that a `MarketOrder`
  cannot carry a price — this should already be true from Task 4's
  type-level test; re-confirm it's actually exercised through
  `submit()`'s `std::visit` dispatch here.

**Implements:** `requirements.md` R9–R11.

---

## Task 12 — `cancel()`

Implement `MatchingEngine::cancel`: R12 (successful O(1) cancel), R13
(unknown ID rejection), R18 (`on_order_cancelled` emission), R19
(rejections don't emit events).

**Acceptance criteria (GoogleTest):**
- Cancel a resting order: removed from book, `on_order_cancelled`
  fires exactly once, `Accepted` returned.
- Cancel an unknown/never-existed ID: `UnknownOrderId`, no event fired.
- Cancel an already-filled order's ID: `UnknownOrderId` (it's no longer
  resting, even though it's in `ever_seen_ids_`) — this is the precise
  test that distinguishes "resting" from "lifetime-unique," worth
  calling out explicitly since it's easy to get backwards.
- Cancel an already-cancelled order's ID (cancel twice): second call
  returns `UnknownOrderId`.

**Implements:** `requirements.md` R12, R13, R18, R19.

---

## Task 13 — Invariant assertions

Add explicit runtime assertions (debug-build `assert`s, not silently
skipped) for the invariants listed in `CHARTER.md`: price levels
sorted, FIFO preserved, quantity/price > 0 for resting orders, book
never left crossed after matching, every resting order belongs to
exactly one price level, `orders_`/intrusive-list agreement. Add tests
that would trip these assertions if the implementation were subtly
wrong (e.g. deliberately probe with a mutation test or a manually
constructed bad state, where feasible).

**Acceptance criteria:** assertions present at the specific points
`design.md`/`CHARTER.md` imply they matter most (post-match, post-
cancel); a debug build with a deliberately introduced bug (e.g. comment
out a `total_qty_` update) causes a test to fail loudly, not silently
pass — confirm this manually once, then revert the deliberate bug.

**Implements:** `CHARTER.md` §Invariants.

---

## Task 14 — `apps/cli/`

Implement `CLIParser` (parses the six commands from `requirements.md`
§7), `ConsolePrinter` (renders `EngineResponse`/`EngineResult`/book
state to stdout), and `main.cpp` wiring them to `MatchingEngine`. No
`EventSink` needed (per §7's explicit decision).

**Acceptance criteria:**
- Manual smoke test: run the CLI, execute the example sequence from
  the doc that started this project (`ADD 1 BUY 10000 50`, `ADD 2 SELL
  10020 30`, etc.) and confirm output matches expected fills/rejections.
- Malformed input (bad command, wrong argument count) produces a clear
  error message, not a crash.

**Implements:** `requirements.md` §7.

---

## Task 15 — ADRs

Write `docs/adr/ADR-001` through `ADR-005` (one page each: Context /
Decision / Alternatives / Consequences) per `CHARTER.md`'s
documentation discipline: integer prices, intrusive lists over
`std::list`, single-threaded-per-symbol, Ports & Adapters, client-
supplied lifetime-unique Order IDs.

**Acceptance criteria:** five files exist, each following the
Context/Decision/Alternatives/Consequences shape, each under ~1 page.

**Implements:** `CHARTER.md` §Documentation Discipline.

---

## Task 16 — GitHub Actions CI

Add `.github/workflows/ci.yml`: build (Ninja/CMake), run
`ctest`, run `clang-tidy`, run `cppcheck` — per `.kiro/steering/tech.md`
and `CHARTER.md`'s "CI from day 1."

**Acceptance criteria:** workflow runs successfully on push (verify via
an actual push once you have a GitHub remote set up), covering build +
test + both static-analysis tools.

**Implements:** `.kiro/steering/tech.md` locked decisions.

---

## Task 17 — Definition of Done audit

Re-read `requirements.md` end to end and confirm every R-number and
NFR has at least one passing test explicitly traceable to it (a simple
table: requirement ID → test name is sufficient, doesn't need to be
fancy). Re-read `design.md` §7's complexity table and confirm each
claim is either tested or at least code-reviewed for the claimed
complexity shape. Confirm `docs/LEARNING.md` has an entry for every
class/type introduced in Tasks 2–14, per `.kiro/steering/learning-doc.md`.

**Acceptance criteria:** the traceability table exists (can live in
`docs/LEARNING.md` or a separate `docs/traceability-phase-01.md`); all
`requirements.md` §8 Test Requirements are satisfied; Phase 1's
Definition of Done (per `PLAN.md`'s phase table) is met.

---

Once Task 17 is signed off, Phase 1 is complete and Phase 2
(benchmarking) can begin — which needs its own `design.md`/`tasks.md`
written first, same process.
