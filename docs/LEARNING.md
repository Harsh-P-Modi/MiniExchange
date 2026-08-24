# MiniExchange — Learning Documentation

This document explains the "why" and "how" behind every significant design decision and implementation detail in MiniExchange. It's written as a tutorial for someone with strong competitive-programming-level C++ and algorithms knowledge, but new to systems/HFT-style engineering.

## Phase 1: Limit Order Book + Matching Engine

### Task 1 — Project Skeleton & Build System

**What it does:**
Establishes the foundational build infrastructure and project structure for MiniExchange. This includes the CMake build configuration, code quality tooling (clang-format, clang-tidy), and the directory structure that houses all Phase 1 components.

**Exact locations:**
- Root `CMakeLists.txt` — build system configuration
- `.clang-format` — automatic code formatting rules
- `.clang-tidy` — static analysis configuration
- `.gitignore` — version control exclusions
- Directory structure: `core/`, `orderbook/`, `engine/`, `interfaces/`, `apps/cli/`, `tests/`, `docs/adr/` (each with `.gitkeep` placeholder)

**Why this architecture:**
The directory structure directly maps to the Ports & Adapters (Hexagonal Architecture) pattern locked in by the steering files:

- `core/` — domain primitives (Order, Trade, Price, etc.) with zero dependencies on other modules
- `orderbook/` — data structure layer, depends only on `core/`
- `engine/` — business logic, depends on `orderbook/` and `core/`
- `interfaces/` — the "ports" (EngineAPI input port, EventSink output port)
- `apps/cli/` — executable composition root, depends inward on `interfaces/`
- `tests/` — test suite
- `docs/adr/` — Architecture Decision Records

This unidirectional dependency flow (`apps/ → interfaces/ → engine/ → orderbook/ → core/`) is deliberate: lower layers never know about upper layers, making each layer independently testable and allowing future phases to add new adapters (TCP gateway, FIX parser, UDP feed) without modifying the engine core.

**Why CMake + Ninja specifically:**
- CMake is the de facto standard for cross-platform C++ builds and has excellent tooling integration
- Ninja is faster than Make for incremental builds (critical for quick iteration during development)
- `FetchContent` (CMake 3.14+) handles dependencies (GoogleTest, Google Benchmark) without requiring pre-installation or manual download, making the project immediately buildable on a fresh machine

**Why C++20:**
Modern C++ features that simplify this project:
- `std::variant` for the `NewOrder` type (LimitOrder vs MarketOrder without a separate type enum)
- Concepts (available if needed in later phases)
- Better constexpr support
- `std::format` (C++20) or structured bindings (C++17, but C++20 gives us the full ecosystem)

The project explicitly sets `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF` to ensure we get pure C++20, not compiler-specific extensions.

**Why `-Wall -Wextra -Wpedantic -Werror`:**
These flags turn every warning into a build error, enforcing clean code from day 1:
- `-Wall` — enables most common warnings
- `-Wextra` — additional warnings not covered by `-Wall`
- `-Wpedantic` — strict ISO C++ compliance warnings
- `-Werror` — treat warnings as errors (prevents "I'll fix that later" technical debt)

This is standard practice in HFT firms where a single uninitialized variable or implicit conversion bug can cost millions in a live trading system.

**Why RelWithDebInfo as default:**
`RelWithDebInfo` compiles with optimizations (`-O2`) *and* debug symbols (`-g`). This is the sweet spot for development:
- Fast enough to run realistic benchmarks (unlike `Debug` builds)
- Debuggable with gdb/lldb when things go wrong (unlike pure `Release` builds)
- Matches how production systems are often deployed (optimized but with symbols for postmortem debugging)

**Why Google C++ Style Guide:**
- Industry standard, especially in companies using C++ at scale
- Balances readability with performance (e.g., allows references and pointers, prefers `snake_case` for functions which is more readable than `camelCase` in long C++ identifiers)
- The `.clang-format` and `.clang-tidy` configs enforce this automatically, so style never becomes a code-review debate

**Why separate `.clang-format` and `.clang-tidy`:**
- `.clang-format` — purely cosmetic (spacing, braces, line breaks). Runs automatically on save in most editors, never fails the build
- `.clang-tidy` — semantic static analysis (null pointer risks, performance anti-patterns, modernization suggestions). Runs in CI and will fail the build on violations

Separating these concerns means developers get instant visual formatting without waiting for expensive analysis, while CI enforces deeper checks.

**Complexity:**
N/A — this task creates configuration, not algorithms.

**Benefits of this choice:**
- **Reproducible builds:** Anyone cloning the repo can run `cmake -S . -B build -G Ninja && cmake --build build` and get a working binary, no manual dependency hunting
- **Enforced quality:** Warnings-as-errors and clang-tidy mean code quality issues are caught immediately, not after they've spread through the codebase
- **Future-proof structure:** The directory layout is planned for all 10 phases — we're not creating `adapters/` folders yet (per the steering file's rule: only create directories when you're about to populate them), but the structure anticipates where they'll go
- **CI-ready:** The CMake setup with GoogleTest integration means CI pipelines (added in Task 16) can run `ctest` with zero additional scripting

**Drawbacks / tradeoffs accepted:**
- **CMake learning curve:** CMake syntax is notoriously cryptic compared to simpler build systems (Make, Meson). However, it's what the industry uses for C++, and recruiters expect to see it
- **FetchContent download time:** On first configure, CMake downloads GoogleTest (~15MB) and Google Benchmark (~5MB). Subsequent builds are instant, but the first one requires internet access. Alternative (vendoring dependencies in `third_party/`) was rejected because it bloats the repo and makes updates harder
- **Ninja requirement:** Developers must install Ninja separately (it's not bundled with CMake). Alternative (plain Unix Makefiles with `-G "Unix Makefiles"`) works but is slower. For a project focused on performance, the faster build tool is worth the extra install step

**Alternatives considered and rejected:**
1. **Bazel** — Google's build system, excellent for monorepos and hermetic builds. Rejected because:
   - Steeper learning curve than CMake
   - Less common outside Google/large companies (most HFT firms use CMake or proprietary build systems)
   - Overkill for a single-repo project with ~10 subdirectories

2. **Meson** — Modern, fast, simpler syntax than CMake. Rejected because:
   - Less industry adoption in HFT/finance (CMake is the expected standard)
   - FetchContent-equivalent (wraps/subprojects) is less mature

3. **Conan/vcpkg for dependencies** — External package managers for C++. Rejected because:
   - FetchContent is simpler (one `CMakeLists.txt`, no separate manifest files)
   - This project has only 2 dependencies, both from Google (GoogleTest, Benchmark) — downloading them directly is fine
   - Production HFT systems often can't use public package registries anyway (air-gapped networks), so showing FetchContent demonstrates understanding of self-contained builds

4. **Header-only structure (no separate `.cpp` files)** — Everything in headers. Rejected because:
   - Longer compile times (every translation unit recompiles all logic)
   - Not representative of real HFT codebases, which carefully manage compilation boundaries to keep incremental build times low

**How this connects to what came before:**
This is the foundational task — nothing precedes it. All future tasks depend on this build system being in place.

**Check your understanding:**
1. Why does the steering file forbid creating `adapters/tcp/` or `apps/replay/` directories in this task, even though we know Phase 5 and later will need them?
2. If you removed `-Werror` from the CMakeLists.txt, what category of bugs might slip through into production that the current setup catches immediately?
3. Why does `FetchContent` download dependencies at *configure* time (when you run `cmake -S . -B build`) rather than at *build* time (when you run `cmake --build build`)?


### Task 2 — `core/Types.hpp`

**What it does:**
Defines the fundamental domain types used throughout the matching engine: `OrderId`, `Price`, `Quantity`, `Side`, `Sequence`, and `TradeSequence`. These are the building blocks of every order, trade, and book operation.

**Exact locations:**
- `core/Types.hpp` (lines 1–145) — all type definitions
- `tests/core_types_test.cpp` — comprehensive test suite verifying type safety and basic operations

**Why strong type wrappers, specifically:**
The core design decision here is: **explicit wrapper structs** (`struct Price { int64_t value; }`) rather than bare type aliases (`using Price = int64_t`). This choice trades a small amount of verbosity for compile-time type safety.

With bare aliases, this compiles but is semantically wrong:
```cpp
using Price = int64_t;
using Quantity = int64_t;

Price p{10000};
Quantity q = p;  // Compiles! But price and quantity are different concepts
if (p == q) {}   // Compiles! But comparing price to quantity makes no sense
```

With strong wrappers, the same code **fails to compile**:
```cpp
struct Price { int64_t value; };
struct Quantity { uint64_t value; };

Price p{10000};
Quantity q = p;  // ERROR: no implicit conversion from Price to Quantity
if (p == q) {}   // ERROR: no operator== between Price and Quantity
```

The compiler enforces that prices and quantities are distinct types, catching an entire class of logic bugs at compile time rather than runtime. A recruiter reviewing this code sees deliberate type-system usage rather than treating everything as "some number."

**Why this architecture:**
All these types live in `core/` because they're pure domain primitives with zero dependencies. They don't "do" anything (no methods beyond comparison/arithmetic operators) — they just *are*. This keeps `core/` as the dependency-free foundation that everything else builds on.

Contrast with putting these in `engine/` or `orderbook/`: that would create circular dependencies (the engine needs `Price` to create orders, but if `Price` were defined in `engine/`, the orderbook would have to depend on the engine to use it — backwards).

**Data Structure Deep Dive:**

**`OrderId` (uint64_t):**
- Client-supplied, not engine-generated
- Unsigned because negative IDs make no sense
- 64 bits gives ~18 quintillion possible IDs (effectively unlimited for a single engine instance)
- Must be hashable for O(1) lookup in `unordered_map` — hence the `std::hash` specialization at the bottom of `Types.hpp`

**`Price` (int64_t, signed):**
- Represents ticks, not dollars (no floating point per the hard rules in `.kiro/steering/tech.md`)
- *Signed* even though resting orders must have `price > 0` — why? Future calculations might compute spread (bid - ask) or price deltas, which can be negative. Using signed avoids casting logic scattered through the codebase
- Comparison operators (`<`, `>`, `<=`, `>=`) are needed because prices form an ordered tree (bids descending, asks ascending) in the order book
- Equality operators (`==`, `!=`) needed for price-level lookup

**`Quantity` (uint64_t, unsigned):**
- Unsigned because negative quantity is nonsensical
- Arithmetic operators (`+`, `-`, `+=`, `-=`) are provided because matching logic constantly updates `remaining_quantity -= fill_quantity`
- The engine must guard against underflow (subtracting more than available) — the unsigned type makes underflow detectable (wraps to huge positive number, which triggers assertions), whereas signed underflow is undefined behavior in C++

**`Side` (enum class):**
- `enum class` rather than plain `enum` — scoped names (`Side::Buy` not just `Buy`) prevent naming collisions and implicit integer conversions
- Only two values: `Buy`, `Sell`. No `Unknown` or `Unspecified` — an order always has a defined side, or it's not valid input

**`Sequence` and `TradeSequence`:**
- Both are monotonically increasing 64-bit counters, but they're distinct types
- `Sequence` orders incoming orders for FIFO tiebreaking within a price level
- `TradeSequence` numbers executed trades for the market data feed (Phase 6) and replay
- Keeping them separate (not one shared "sequence" counter) means a design change to one (e.g., making `Sequence` wrap at 2^32 for memory reasons) doesn't inadvertently affect the other
- Both provide `operator++` (pre and post-increment) so the engine can write `next_sequence_++` naturally

**Hash specialization for `OrderId`:**
The `std::hash<miniexchange::OrderId>` specialization (lines 137–143) lives outside the `miniexchange` namespace, in namespace `std`. This is the standard C++ way to make a custom type usable in `std::unordered_map` and `std::unordered_set`.

It simply forwards to `std::hash<uint64_t>`, since `OrderId` is fundamentally a `uint64_t` wrapper. The alternative (defining a custom hasher struct every time we declare an `unordered_map<OrderId, ...>`) would clutter every usage site.

**Complexity:**
- **Time:** All operations are O(1) — wrappers compile to zero-cost abstractions (no vtables, no heap allocation, just direct access to the underlying integer)
- **Space:** Each type is exactly `sizeof(uint64_t)` or `sizeof(int64_t)` (8 bytes on 64-bit systems) — no overhead from the wrapper struct

Modern compilers (GCC, Clang, MSVC with optimizations enabled) treat these wrappers as transparent at runtime. The `constexpr` constructors and operators allow compile-time evaluation where possible.

**Benefits:**
1. **Compile-time type safety:** Mixing up price and quantity (or OrderId and Sequence) is caught instantly, before any tests run
2. **Self-documenting code:** Function signature `void submit(OrderId id, Price price, Quantity qty)` is clearer than `void submit(uint64_t, int64_t, uint64_t)` — no need to remember parameter order or units
3. **Zero runtime cost:** Strong types compile to the same assembly as bare integers (verified with `godbolt.org` — the test assertions exist to keep this promise)
4. **Easy refactoring:** If Phase 3 decides `Price` should be a 32-bit int (maybe to fit more orders in cache), changing one line in `Types.hpp` updates the entire codebase — no grep-and-replace hunting for "which int64_t was a price"

**Drawbacks / tradeoffs accepted:**
1. **Explicit construction required:** You must write `Price{10000}`, not just `10000`. This is verbose but intentional — implicit conversions are the enemy of type safety
2. **Can't directly use in generic code expecting integers:** If a third-party library expects `int64_t`, you must explicitly pass `.value`. This is rare in practice (our code mostly passes these types around internally)
3. **More boilerplate:** Each type needs comparison operators, arithmetic operators, etc. Alternative (using a template like `StrongTypedef<int64_t, PriceTag>`) was considered but rejected as over-engineering for 6 types — explicit is fine at this scale

**Alternatives considered and rejected:**

1. **Bare type aliases (`using Price = int64_t;`):**
   - Pros: Less code, no explicit construction, works with generic code expecting integers
   - Cons: Zero type safety — `Price p = quantity;` compiles and is silently wrong. This is the exact category of bug that strong types prevent
   - Rejected because type safety is the entire point of this design decision

2. **Boost.StrongTypedef or similar template library:**
   - Pros: Less boilerplate per type (one line: `BOOST_STRONG_TYPEDEF(int64_t, Price)`)
   - Cons: External dependency (Boost is huge), opaque compiler errors, harder to customize operators
   - Rejected because this project minimizes dependencies (only GoogleTest and Benchmark), and 6 simple structs aren't worth a macro library

3. **Separate `PriceTag`, `QuantityTag` empty structs + template:**
   ```cpp
   template<typename T, typename Tag>
   struct StrongType { T value; /* operators */ };
   using Price = StrongType<int64_t, struct PriceTag>;
   ```
   - Pros: Eliminates per-type boilerplate (define operators once in the template)
   - Cons: All types end up with the same operators (but `Sequence` shouldn't have arithmetic, `Price` doesn't need increment). Harder to extend selectively
   - Rejected as premature abstraction — explicit definitions let each type expose only its meaningful operations

4. **`enum class` instead of structs:**
   ```cpp
   enum class Price : int64_t {};
   ```
   - Pros: Strong typing, no construction syntax needed for literals
   - Cons: Can't add operators or methods. `enum class` arithmetic requires casting (`Price{static_cast<int64_t>(p) + 1}`) which is worse than `Price{p.value + 1}`
   - Rejected because `enum class` is semantically for enumerated values (Buy/Sell), not opaque wrappers around numeric types

5. **Always passing by value vs. const reference:**
   - Current choice: pass by value (e.g., `Price price`) for these 8-byte types
   - Alternative: `const Price& price` to avoid copies
   - Decision: 8 bytes fits in a register; passing by reference (8-byte pointer) on 64-bit systems is no cheaper and adds indirection. Standard C++ guideline: pass by value for types <= 2 words (16 bytes on 64-bit). If Phase 3 makes `Order` huge, we'd pass `const Order&`, but primitives stay by-value

**How this connects to what came before:**
Task 1 created the build system and directory structure. Task 2 is the first actual *code* — the foundation types that every future task (Order, Trade, OrderBook, MatchingEngine) will use.

**Check your understanding:**
1. Why is `Price` signed (`int64_t`) even though the requirements say resting orders must have `price > 0`? What operation would be harder if it were `uint64_t`?
2. The `std::hash` specialization for `OrderId` is defined *outside* the `miniexchange` namespace (it's in `namespace std`). Why can't it be inside `namespace miniexchange`?
3. If you uncommented the line `Price p = Quantity{100};` in the test file, would the error happen at compile time or runtime? How does the strong-type design guarantee this?
4. Why does `Quantity` provide `operator-=` but `OrderId` does not? What would it mean to subtract order IDs, and why doesn't that operation make sense?


### Task 3 — `core/Trade.hpp`

**What it does:**
Defines the `Trade` struct, which represents a single executed fill between a buy order and a sell order. This is the canonical record of "two orders matched at this price for this quantity," used everywhere a trade needs to be communicated: returned to the caller in `EngineResponse`, emitted to observers via `EventSink::on_trade`, logged, sent over the UDP market-data feed (Phase 6), and written to FIX execution reports (Phase 9).

**Exact locations:**
- `core/Trade.hpp` (lines 1–28) — the `Trade` struct definition
- `tests/core_trade_test.cpp` (lines 1–44) — test suite verifying field construction and access

**Why this data structure, specifically:**
A `Trade` is a plain aggregate struct (no methods, no private members) with five fields, each serving a distinct purpose:

1. **`TradeSequence trade_sequence`** — unique, monotonically increasing trade ID. Every fill gets its own sequence number, assigned by the engine at execution time. This serves two purposes:
   - **Market data ordering:** Phase 6's UDP feed sends trades in sequence-number order. If a packet is lost, the receiver detects the gap (sequence 100, 101, 103... 102 missing) and can request a retransmit
   - **Replay determinism:** Replaying a sequence of orders from a log must produce the exact same `trade_sequence` numbers, allowing post-trade analysis to match "what happened live" with "what the replay says happened"

2. **`OrderId buy_order_id`** and **`OrderId sell_order_id`** — which two orders participated in this trade. Every trade has exactly one buy and one sell (this is a central limit order book, not an auction).
   
   **Design decision: buy/sell representation, not aggressor/resting.** An alternative representation would be `aggressor_order_id` (the incoming order that triggered the match) and `passive_order_id` (the resting order that got filled). Why wasn't that chosen?
   
   - **External consumers don't care about matching mechanics.** The CLI user, the UDP feed subscriber, the FIX client — none of them need to know "which order arrived second." They care about "this buy order crossed this sell order." Aggressor/passive is an *internal* matching-engine concept; buy/sell is the *external* trade representation
   - **Replay and audit consistency.** A trade log that records "buy 1001 crossed sell 2002 at 10000 for 50" is unambiguous and matches exchange conventions. A log that says "aggressor 1001 crossed passive 2002" requires context about which direction 1001 was — if you don't have that context (e.g., reading just the trade log, not the order log), the trade is ambiguous
   - **This mirrors real exchange behavior.** CME, NASDAQ, etc. report trades as "buy order X, sell order Y, price, quantity" in their market data feeds — not as "aggressor/passive"

3. **`Price price`** — the execution price for this trade. Per `requirements.md` R6, this is always the *resting* order's price, not the incoming order's price (price-time priority: the resting order "had it first"). For a single incoming order that crosses multiple price levels (e.g., a large buy sweeping through 10015, 10016, 10017), each fill gets its *own* trade at *that level's* price — not one aggregated trade at some average.

4. **`Quantity quantity`** — how much was filled in this particular trade. If an incoming order for 100 shares matches two resting orders (one for 30, one for 70), the result is *two* `Trade` structs: one with `quantity = 30`, one with `quantity = 70`. The `EngineResponse` returned to the caller aggregates these into a `vector<Trade>`, but `EventSink::on_trade` sees each individually (per R17 in `requirements.md`) — this is deliberate, so market-data subscribers get tick-by-tick granularity, not just summary-per-order.

**Why this architecture:**
`Trade` lives in `core/` (not `engine/` or `interfaces/`) because it's a pure data structure with no dependencies — just a bundle of five fields. It doesn't "do" anything; it's passed around by value (it's only 40 bytes on a 64-bit system: 5 × 8 bytes) and copied freely. Making it a `core/` type means:

- `engine/` creates `Trade` instances
- `interfaces/EventSink` references `Trade` in its `on_trade(const Trade&)` signature
- `apps/cli/` formats `Trade` for display
- Phase 6's UDP adapter serializes `Trade` to the wire
- Phase 9's FIX adapter maps `Trade` to FIX ExecutionReport (tag 35=8)

None of these layers need to depend on any other — they all just depend on `core/Trade.hpp`, the shared vocabulary. If `Trade` lived in `engine/`, the CLI would have to depend on the engine (backwards), or we'd need two near-duplicate types (`engine::Trade` and `core::TradeSummary`), which is worse.

**`Fill` vs. `Trade` unification:**
An early draft of `requirements.md` had both `Fill` (what the engine returns to the caller) and `Trade` (what the market-data feed emits), with identical fields. The final design unified them: there's one canonical `Trade` type, used everywhere. This is simpler (one struct to maintain, not two) and matches real exchange architecture — internally, a trade is a trade; formatting it for FIX vs. formatting it for a CSV log is a presentation concern, not a reason for a separate type.

**Complexity:**
- **Time:** O(1) to construct, O(1) to copy (it's a plain-old-data struct, no heap allocation, no pointer chasing)
- **Space:** 40 bytes (5 × `uint64_t` or `int64_t` fields), plus padding if the compiler adds alignment gaps (likely none, since all fields are 8-byte aligned naturally). Modern x86-64 CPUs can copy this in a single 256-bit SIMD move if the compiler chooses

**Benefits:**
1. **Single source of truth:** One `Trade` definition for the entire project. No `engine::Fill` vs. `market_data::Trade` drift over time
2. **Canonical buy/sell representation:** Trades are unambiguous in logs, replay, and external feeds without needing the order history for context
3. **Separate trade and order sequence counters:** `TradeSequence` is independent from `Sequence` (order FIFO numbering). If Phase 6 needs to reset trade numbering daily (e.g., for daily market-data file conventions), that doesn't affect order matching behavior. Conversely, if we later discover that 32-bit `Sequence` is enough for FIFO (since it only needs to be unique per price level within a day), shrinking it doesn't change the trade log format
4. **Lightweight:** 40 bytes is small enough to pass by value (via `std::vector<Trade>` in `EngineResponse`) without worrying about copy overhead. For comparison, a `std::string` is 24–32 bytes *plus* the heap-allocated buffer, and we routinely pass those by value in non-hot paths

**Drawbacks / tradeoffs accepted:**
1. **No embedded timestamp:** `Trade` doesn't include a `timestamp` field. This was a deliberate choice per NFR2 in `requirements.md`: the engine doesn't read wall-clock time (determinism requirement). When a trade needs a timestamp (e.g., for the UDP feed or a CSV log), the `apps/*` or `adapters/*` layer adds it at emission time, using whatever clock is appropriate for that context (system clock for live, simulated clock for replay). Alternative (embedding `std::chrono::time_point` in `Trade`) was rejected because it would force the engine to know about time, violating the "engine is a pure function of its inputs" principle
2. **No side information (Buy/Sell enum in the Trade itself):** The `Trade` struct records *which* order was the buy and *which* was the sell via `buy_order_id` and `sell_order_id`, but it doesn't redundantly include a `Side` field. You derive the side by looking up the order ID. Alternative (adding `Side side_of_aggressor`) was rejected because it's redundant (you can always compute it from the order IDs if you have the order log) and adds 8 bytes of padding (enums are typically 4 bytes, but the struct would pad to 48 bytes for alignment). Since this struct is copied a lot (every trade goes into `EngineResponse.trades` and also gets emitted via `EventSink`), saving 8 bytes per trade matters
3. **Can't directly tell "was this a fill or a partial fill" from the Trade alone:** If an incoming order for 100 matches a resting order for 30, the resulting `Trade` just says `quantity = 30`. You can't tell from the `Trade` itself whether the incoming order wanted 30 (fully filled) or 100 (partially filled). That context lives in `EngineResponse.remaining_qty`, which the caller gets synchronously. `Trade` is purely "what executed," not "what was intended"

**Alternatives considered and rejected:**

1. **Separate `Fill` and `Trade` types:**
   - Original proposal: `Fill` (internal engine type) and `Trade` (market-data type) with identical fields
   - Rejected because: same fields, same semantics — maintaining two types is pure overhead. If they ever diverge, *that's* when we split them

2. **Aggressor/passive representation instead of buy/sell:**
   ```cpp
   struct Trade {
       TradeSequence trade_sequence;
       OrderId aggressor_order_id;
       OrderId passive_order_id;
       Price price;
       Quantity quantity;
   };
   ```
   - Rejected because: aggressor/passive is an internal matching detail, not the external trade representation. Real exchanges report buy/sell; our trade log should match that convention

3. **Embed the full `Order` structs instead of just IDs:**
   ```cpp
   struct Trade {
       Order buy_order;  // full copy
       Order sell_order;
       Price price;
       Quantity quantity;
   };
   ```
   - Rejected because: `Order` includes intrusive list pointers (`prev`/`next`) and a back-pointer to `PriceLevel` (see Task 4's design). Copying those into a `Trade` is semantically wrong (the `Trade` doesn't "own" those linkages) and expensive (`Order` is ~80 bytes vs. 40 bytes for just the IDs). If a consumer needs full order details, they query the book by ID

4. **Add a `timestamp` field:**
   - Rejected per NFR2: the engine doesn't read wall-clock time. Timestamps are added by the `apps/` layer at emission time, not baked into the core type

5. **Add a `Side` enum to avoid "which ID is the buy":**
   - Rejected because: `buy_order_id` and `sell_order_id` already encode the side unambiguously. Adding a redundant `Side aggressor_side` field wastes space (8 bytes padding for a 4-byte enum) and creates a new invariant to maintain (does `aggressor_side == Buy` match `aggressor_order_id == buy_order_id`? If not, which one is wrong?). Simpler to have one representation

**How this connects to what came before:**
Task 2 created the primitive types (`TradeSequence`, `OrderId`, `Price`, `Quantity`). Task 3 composes them into the first *business domain* type: a trade is what happens when orders match. Task 4 will create `Order` (what rests in the book), and then Task 5 will create `EngineResponse` (which bundles a `vector<Trade>` with the result status).

**Check your understanding:**
1. Why does `Trade` use `buy_order_id` and `sell_order_id` rather than `aggressor_order_id` and `passive_order_id`? What breaks if you only have the trade log (not the order log) and the trades use aggressor/passive representation?
2. The engine maintains two separate monotonic counters: `Sequence` (for FIFO ordering of resting orders) and `TradeSequence` (for numbering executed trades). Why not use one shared counter for both? What would change if we did?
3. A large incoming buy order sweeps through three resting sell orders at prices 10015, 10016, and 10017. How many `Trade` structs does this produce, and what is the `price` field in each one?
4. Why doesn't `Trade` include a timestamp, given that every real exchange's trade log has timestamps? Where will timestamps get added in this architecture?


### Task 4 — `core/Order.hpp` and `core/NewOrder.hpp`

**What it does:**
Defines two fundamental type systems for representing orders in MiniExchange:
1. **`Order`** — the resting order struct that lives in the book's intrusive doubly-linked lists
2. **`NewOrder`** — the discriminated union (`std::variant<LimitOrder, MarketOrder>`) that clients submit via `EngineAPI::submit`

**Exact locations:**
- `core/Order.hpp` (lines 1–47) — the `Order` struct with intrusive list pointers
- `core/NewOrder.hpp` (lines 1–48) — `LimitOrder`, `MarketOrder`, and `NewOrder` variant
- `tests/test_order_types.cpp` (lines 1–93) — comprehensive test suite including compile-time verification that `MarketOrder` has no price member

**Why this data structure, specifically:**

**The `Order` struct:**
```cpp
struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
    Sequence sequence;
    Order* prev = nullptr;
    Order* next = nullptr;
    PriceLevel* level = nullptr;
};
```

This is the *resting* order representation — what lives in the order book after an order is accepted and (if not immediately filled) added to a price level's FIFO queue. Each field serves a distinct purpose:

- **`id`, `side`, `price`, `quantity`** — the order's business data (what the client submitted)
- **`sequence`** — FIFO tiebreaking. When two orders arrive at the same price, the one with the lower `sequence` number (arrived first) fills first. Assigned by the engine at insertion time, not by the client
- **`prev`, `next`** — intrusive doubly-linked list pointers. This Order *is* a node in the price level's queue. There's no separate `Node<Order>` struct wrapping it — the linkage pointers live directly in the order data
- **`level`** — back-pointer to the `PriceLevel` this order belongs to. Needed for O(1) cancel (see detailed rationale below)

**Why intrusive list, not `std::list`:**

The steering file (`tech.md`) has a hard rule: "No `std::list`, ever." Why?

`std::list<Order>` allocates a separate heap node for every list element:
```
Heap:
  [Node1: prev*, next*, Order data] -> [Node2: prev*, next*, Order data] -> ...
```

Intrusive list embeds the pointers in the data itself:
```
Heap:
  [Order: id, side, price, qty, sequence, prev*, next*, level*]
```

**Benefits of intrusive:**
1. **One allocation per order, not two.** `std::list` allocates the `Order` *and* a separate node. Intrusive list allocates only the `Order` itself. Phase 3's memory pool will pre-allocate a slab of `Order` structs; with `std::list`, we'd need a second pool for list nodes, doubling complexity
2. **Better cache locality.** Traversing a `std::list` means chasing three pointers per node (node → data, node → next node → data, ...). Intrusive list traversal is `order->next->next` — two pointers, not three, and the data is always "right here" with the linkage
3. **No allocator policy needed.** `std::list` requires you to specify an allocator (or accept `std::allocator`, which calls `malloc`). Intrusive list ownership is explicit: the `unordered_map<OrderId, unique_ptr<Order>>` owns the `Order`, the list pointers are just non-owning views into that map

**Drawbacks:**
1. **Cannot belong to multiple lists simultaneously.** An `Order` has one `prev` and one `next`, so it can be in exactly one list at a time. `std::list` nodes can be copied into multiple lists. For our use case (one order, one price level), this is fine — if Phase 8 needs "all orders for this client" as a separate index, that index would be a `vector<Order*>` or another hash map, not a second intrusive list
2. **Manual memory management of linkage.** Inserting and removing from `std::list` is `list.push_back(order)` — automatic. Intrusive list insertion requires manually setting `order->prev`, `order->next`, `tail->next = order`, etc. This logic lives in `orderbook/PriceLevel`, not scattered through user code, so the complexity is contained

**Why the `level` back-pointer:**

When the engine receives a cancel request for `OrderId = 42`, it must:
1. Look up `Order* order` via `unordered_map<OrderId, unique_ptr<Order>>`
2. Unlink the order from its price level's intrusive list (update `prev->next`, `next->prev`)
3. Decrement that level's aggregate `total_quantity`
4. If the level is now empty, remove the level from the price tree

Without the `level` back-pointer, step 2/3 would require knowing the order's price, then looking up `PriceLevel* level` in the price tree (O(log P)), then unlinking. **With** the back-pointer, we have `level` directly: `order->level->remove(order)`, no tree traversal needed — O(1).

**Cost:** 8 bytes per `Order`. For a typical `Order` that's ~64 bytes (8 fields × 8 bytes), this is a 12.5% overhead. **Benefit:** O(1) cancel per charter requirements (`CHARTER.md` complexity table: "Cancel — O(1) amortized").

**Alternative rejected:** Store `price` in the `Order` (wait, we already do), then look up the level via `book.find_level(order->side, order->price)` on cancel. Cost: O(log P) tree traversal. Rejected because the charter explicitly targets O(1) cancel.

**The `NewOrder` variant:**

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
    Quantity quantity;  // no price field — structurally enforced
};

using NewOrder = std::variant<LimitOrder, MarketOrder>;
```

This is what clients submit to `EngineAPI::submit(const NewOrder&)`. It's a discriminated union: a `NewOrder` is *either* a `LimitOrder` *or* a `MarketOrder`, never both, never neither.

**Why `std::variant`, not a flat struct with an enum:**

Alternative design (rejected):
```cpp
enum class OrderType { Limit, Market };

struct NewOrder {
    OrderType type;
    OrderId id;
    Side side;
    Price price;       // ignored if type == Market ❌
    Quantity quantity;
};
```

Problem: `price` is always present, even for market orders. Requirements R4 says "invalid price is rejected with `InvalidPrice`," but R11 says "Market orders carry no price." With the flat struct, you'd need a runtime check:
```cpp
if (order.type == Market && order.price != 0) {
    // Client passed a price for a market order — reject? Ignore?
}
```

With `std::variant<LimitOrder, MarketOrder>`:
```cpp
MarketOrder m{OrderId{1}, Side::Buy, Quantity{100}};
// m.price  ← COMPILE ERROR: MarketOrder has no member 'price'
```

**"Market order with a price" is impossible to construct.** Requirements R11 ("Market orders carry no price") is enforced by the type system, not by runtime validation.

**How the engine uses it:**

`MatchingEngine::submit(const NewOrder& order)` dispatches via `std::visit`:
```cpp
EngineResponse MatchingEngine::submit(const NewOrder& order) {
    return std::visit([this](auto&& o) -> EngineResponse {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, LimitOrder>) {
            return submit_limit(o);
        } else {
            return submit_market(o);
        }
    }, order);
}
```

`std::visit` is the C++17/20 pattern for "call the right function depending on which alternative the variant holds." It's more verbose than a switch on an enum, but it's **exhaustive by construction** — if we add a third order type (`StopOrder`) to the variant later, any `std::visit` that doesn't handle it fails to compile (unlike a switch, where a missing case is a silent runtime fall-through risk).

**Cost:** `std::variant` adds 8 bytes of overhead (the discriminator tag that tracks "which alternative am I holding"). A `LimitOrder` is 32 bytes (4 × 8-byte fields), so `NewOrder` is 40 bytes. The flat-struct-with-enum alternative would also be 40 bytes (1 byte enum + 7 bytes padding + 4 × 8-byte fields), so there's no space difference — the benefit is purely compile-time safety.

**Why this architecture:**

Both `Order` and `NewOrder` live in `core/` because they're pure data (no methods, no logic). The distinction:
- **`NewOrder`** — what comes *in* (client's submission)
- **`Order`** — what lives *inside* the book (resting order with sequence number and linkage)

Separating these types makes the transformation explicit: `EngineAPI::submit(NewOrder)` → engine validates → assigns `Sequence` → creates `Order` → inserts into book. If we used one `Order` type for both "input" and "resting," we'd need sentinel values for "not assigned yet" fields (e.g., `sequence = INVALID`), which is error-prone. Two types, one conversion, clearer semantics.

**Complexity:**
- **Time:** 
  - Constructing a `LimitOrder`/`MarketOrder` or `Order`: O(1) (just initialize fields)
  - `std::visit` dispatch: O(1) at runtime (compiles to a jump-table or inline if/else)
  - Intrusive list insert/remove: O(1) (pointer fixups, see Task 7 for implementation)
- **Space:** 
  - `Order`: 64 bytes (8 fields × 8 bytes)
  - `NewOrder` (`LimitOrder`): 40 bytes (discriminator + 4 fields)
  - `NewOrder` (`MarketOrder`): 32 bytes (discriminator + 3 fields) padded to 40 for variant alignment

**Benefits:**
1. **Intrusive list = one allocation per order.** Phase 3's memory pool will amortize even that to O(1) lock-free pop from a free-list
2. **O(1) cancel via `level` back-pointer.** No tree traversal needed
3. **Compile-time enforcement of "market orders have no price."** Type system prevents a whole class of invalid input
4. **Separation of concerns:** `NewOrder` is input vocabulary; `Order` is internal book state. Changes to one (e.g., adding `client_id` to `NewOrder` for Phase 8's risk checks) don't force changes to the other

**Drawbacks / tradeoffs accepted:**
1. **`Order` is 64 bytes, not 32.** The `prev`/`next`/`level` pointers are "overhead" from a pure-data perspective. Alternative (store orders in a vector, use indices instead of pointers) was rejected because pointer-based intrusive lists are simpler and faster (no index validation, no indirection through a vector)
2. **Manual intrusive list management.** `std::list::push_back` is one line; intrusive insertion is 5 lines of pointer fixups. Acceptable because the complexity is contained in `PriceLevel::push_back` (Task 7) — callers just call that function, they don't see the pointer manipulation
3. **`std::visit` boilerplate.** The lambda in `submit()` is verbose compared to `if (order.type == Market)`. Cost: ~8 lines of template ceremony. Benefit: exhaustive case checking and compile-time dispatch. Tradeoff accepted because there's only *one* call site (the engine's `submit` function) that needs it — everywhere else just passes `NewOrder` by reference without inspecting it

**Alternatives considered and rejected:**

1. **`std::list<Order>` instead of intrusive:**
   - Pros: Standard library, automatic memory management, well-tested
   - Cons: Two allocations per order (the `Order` and the list node), worse cache locality, incompatible with Phase 3's pool allocator
   - Rejected per steering file hard rule and performance rationale above

2. **No `level` back-pointer, look up the level on cancel:**
   ```cpp
   // On cancel(OrderId id):
   Order* order = orders_.at(id);
   PriceLevel* level = (order->side == Buy ? bids_ : asks_).at(order->price);
   level->remove(order);
   ```
   - Pros: Saves 8 bytes per `Order`
   - Cons: O(log P) tree lookup for every cancel (charter targets O(1))
   - Rejected because cancel performance is a stated requirement

3. **Flat `NewOrder` struct with `OrderType` enum, ignore `price` for market orders:**
   ```cpp
   struct NewOrder {
       OrderType type;
       OrderId id;
       Side side;
       Price price;     // "unused if type == Market"
       Quantity quantity;
   };
   ```
   - Pros: No `std::visit`, no template ceremony, 32 bytes instead of 40
   - Cons: `price` field is always present. "Market order with price = 10000" compiles and requires runtime validation to reject. Violates R11's "Market orders carry no price" structurally
   - Rejected because compile-time safety is worth 8 bytes and `std::visit` verbosity

4. **Three separate submit functions instead of variant:**
   ```cpp
   class EngineAPI {
       virtual EngineResponse submit_limit(const LimitOrder&) = 0;
       virtual EngineResponse submit_market(const MarketOrder&) = 0;
   };
   ```
   - Pros: No variant, no `std::visit`, type-safe dispatch
   - Cons: The CLI parser now needs to call the right function (duplicating the "which type is this?" logic). With `NewOrder`, parsing produces *one* object that the engine dispatches internally — separation of concerns
   - Rejected because the variant centralizes type dispatch in the engine, not scattered across every caller

5. **Use `std::optional<Price>` in a single `NewOrder` struct:**
   ```cpp
   struct NewOrder {
       OrderId id;
       Side side;
       std::optional<Price> price;  // nullopt for market orders
       Quantity quantity;
   };
   ```
   - Pros: One struct, no variant, explicit "no price" representation
   - Cons: `std::optional` adds overhead (16 bytes for 8-byte `Price` + 1-byte flag + padding), and callers must remember to check `.has_value()` before accessing price — runtime check, not compile-time
   - Rejected because the variant approach makes "limit vs. market" a type distinction, not a runtime nullable-field distinction

**Compile-time verification mechanism:**

The test suite (Task 4's acceptance criteria) includes this check:
```cpp
template <typename T>
concept HasPriceMember = requires(T t) {
    { t.price } -> std::convertible_to<Price>;
};

static_assert(!HasPriceMember<MarketOrder>, "MarketOrder must not have a price member");
static_assert(HasPriceMember<LimitOrder>, "LimitOrder must have a price member");
```

**Decision: C++20 concepts over SFINAE.**

Alternative (C++17-compatible, using SFINAE):
```cpp
template <typename T, typename = void>
struct has_price_member : std::false_type {};

template <typename T>
struct has_price_member<T, std::void_t<decltype(std::declval<T>().price)>> : std::true_type {};

static_assert(!has_price_member<MarketOrder>::value);
```

**Why concepts (chosen):**
- More readable: `HasPriceMember<T>` vs. `has_price_member<T>::value`
- Better compiler errors: "constraint not satisfied: HasPriceMember<MarketOrder>" vs. 50 lines of template instantiation errors
- This project uses C++20 (locked in Task 1), so concepts are available

**Why this matters:**
If someone accidentally adds `Price price;` to `MarketOrder`, this static_assert **fails the build immediately**, before any tests run. It's a guardrail: the type system enforces the requirement, and the test suite verifies that the guardrail is in place.

**How this connects to what came before:**

- Task 2 created `OrderId`, `Price`, `Quantity`, `Side`, `Sequence` — the primitive types that `Order` and `NewOrder` compose
- Task 3 created `Trade` — what happens when orders match
- Task 4 creates `Order` (what rests in the book) and `NewOrder` (what clients submit)
- Task 5 (next) will create `EngineResponse` (bundles trades + status) and event payloads, completing the core type system

**Check your understanding:**

1. **Why does `Order` have `prev`/`next` pointers instead of being stored in `std::list<Order>`?** What changes in Phase 3 (memory pool) would be harder if we used `std::list`?

2. **The `level` back-pointer adds 8 bytes to every `Order` (12.5% overhead). Why is this accepted rather than looking up the level via `book.find_level(order->price)` on cancel?** What's the complexity difference?

3. **Why is `NewOrder` a `std::variant<LimitOrder, MarketOrder>` instead of a flat struct with `OrderType` enum and an optional `price` field?** Construct a specific example of invalid input that compiles with the flat-struct approach but fails to compile with the variant approach.

4. **The test uses a C++20 concept (`HasPriceMember<T>`) to verify at compile time that `MarketOrder` has no `price` member. Could this check be done at runtime (in a unit test that runs and asserts)? If not, why not? If yes, why is compile-time better?**

5. **Why are `NewOrder` (input) and `Order` (resting) separate types instead of one `Order` type used for both?** What field exists in `Order` but not in `LimitOrder`, and why does that difference matter?


### Task 6 — `interfaces/EventSink.hpp` and `interfaces/EngineAPI.hpp`

**What it does:**
Defines the two "ports" that form the backbone of MiniExchange's hexagonal architecture. `EventSink` is the output port — anything that wants to observe every state change (trades, acceptances, cancellations) implements it. `EngineAPI` is the input port — anything that wants to submit or cancel orders calls through it. `NullEventSink` is a no-op concrete `EventSink` used as the default when nothing needs to observe.

**Exact locations:**
- `interfaces/event_sink.hpp` — `EventSink` abstract base class + `NullEventSink` singleton (lines 1–83)
- `interfaces/engine_api.hpp` — `EngineAPI` abstract base class (lines 1–62)
- `tests/test_interfaces.cpp` — compile-time structural checks + runtime no-crash + `RecordingEventSink` test (lines 1–99)

---

#### `EventSink` — the output port

`EventSink` has three pure virtual methods:

```cpp
class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_trade(const Trade& trade) = 0;
    virtual void on_order_accepted(const OrderAccepted& event) = 0;
    virtual void on_order_cancelled(const OrderCancelled& event) = 0;
};
```

**Why pure virtual, not default no-op bodies?**

The requirements document (§3) shows an `EventSink` with default no-op bodies:
```cpp
virtual void on_order_accepted(const OrderAccepted&) {}
virtual void on_trade(const Trade&) {}
virtual void on_order_cancelled(const OrderCancelled&) {}
```

That design allows "selective override" — a market-data adapter that only cares about trades overrides only `on_trade` and inherits no-ops for the other two. Sounds convenient. But it has a subtle problem: **it makes bugs silent**. If you write `class MyAdapter : public EventSink` and misspell `on_trad` instead of `on_trade`, the inherited no-op silently takes over and you get no compile error and no trades.

The chosen design — **all pure virtual, with `NullEventSink` as the explicit no-op** — forces every implementor to consciously declare what they handle. If `MyAdapter` only cares about trades, it writes:

```cpp
class MyAdapter : public EventSink {
    void on_trade(const Trade& t) override { /* real logic */ }
    void on_order_accepted(const OrderAccepted&) override {}  // explicit no-op
    void on_order_cancelled(const OrderCancelled&) override {} // explicit no-op
};
```

The two empty overrides aren't noise — they're documentation: "I know about these events and I've decided not to handle them." A typo in `on_trad` would now fail to compile rather than silently calling the base.

**Why this architecture — two channels, not one:**

This is the most important design decision in the whole interface layer. Why does `MatchingEngine::submit()` return an `EngineResponse` **and also** call `EventSink`? Couldn't you just have one mechanism?

The answer is that they serve two fundamentally different audiences:

- **`EngineResponse`** is for the *immediate caller* of `submit()`. When the CLI submits order 42, it needs to know right now: "was it accepted? did it fill? how much is left?" No other party needs this — nobody else was waiting for *this particular call's result*.

- **`EventSink`** is for *any observer*, including those who didn't submit anything. When a market-data adapter is publishing the live order book to subscribers, it needs to know about *every* trade, acceptance, and cancellation — including those caused by other participants' orders. The adapter wasn't the one calling `submit()`, so `EngineResponse` is invisible to it.

If you collapsed them into one mechanism:
- Using only `EngineResponse`: the market-data adapter would have to call `submit()` itself (which it doesn't), or poll a global event log (O(n) per query, defeats the point of a push-based architecture).
- Using only `EventSink`: the CLI would have to register as a listener, then correlate which event corresponds to its own submission, then reconstruct the result. This is complex and race-prone.

Two small, focused channels — each perfect for its audience — beats one large general-purpose channel. This is the Ports & Adapters pattern in practice.

**Why `interfaces/` depends only on `core/`, never on `engine/` or `orderbook/`:**

The dependency direction in this project is: `apps/` → `interfaces/` → `core/`. If `interfaces/` depended on `engine/`, then every adapter would transitively depend on the engine's concrete implementation, defeating the entire purpose of having an interface. Changing the engine's internal allocation strategy (Phase 3), or adding lock-free dispatch (Phase 4), would require recompiling every adapter. As it stands, adapters see only the port — the engine is an implementation detail they're isolated from.

---

#### `NullEventSink` — the default no-op

```cpp
class NullEventSink final : public EventSink {
public:
    static NullEventSink* instance() {
        static NullEventSink sink;
        return &sink;
    }
    void on_trade(const Trade&) override {}
    void on_order_accepted(const OrderAccepted&) override {}
    void on_order_cancelled(const OrderCancelled&) override {}
private:
    NullEventSink() = default;
    NullEventSink(const NullEventSink&) = delete;
    NullEventSink& operator=(const NullEventSink&) = delete;
};
```

**Why a singleton, and why this specific singleton pattern (Meyer's)?**

The engine needs a non-null `EventSink*` to call through. When no real observer is wired up (unit tests that only check `EngineResponse`, the CLI app in Phase 1), we need a safe, cheap default. The options are:

1. **`nullptr` check before every call** — adds branching in the hot matching loop. Every `sink_->on_trade(t)` becomes `if (sink_) sink_->on_trade(t)`. Clutters the code and costs a branch per trade.

2. **A local `NullEventSink` per test/caller** — fine, but someone has to remember to construct and pass one. That's friction.

3. **A global or static `NullEventSink` instance** — zero cost, always available. The question is how to implement "global" safely.

**Meyer's singleton** (function-local static) is the C++11 canonical answer:
```cpp
static NullEventSink* instance() {
    static NullEventSink sink;  // constructed exactly once, on first call
    return &sink;
}
```

- **Thread-safe:** C++11 guarantees that function-local statics are initialized exactly once, atomically, even if multiple threads race to call `instance()` simultaneously. No `std::once_flag` or `std::mutex` needed.
- **Lazy:** constructed only when first needed, not at program startup (avoids static initialization order fiasco).
- **No dynamic allocation:** `sink` is a local static, allocated in the BSS segment, not the heap. `instance()` returns its address — no `new`, no `delete`, no leak possible.
- **Non-copyable:** the private copy constructor and assignment operator deleted prevent accidental copies. Since `NullEventSink` is a singleton, copying it would create a second instance, which is semantically wrong.

**Why `final`:** marking `NullEventSink` as `final` tells the compiler "nothing inherits from this." This enables devirtualization — the compiler can replace virtual dispatch (`vtable lookup`) with a direct call when it can prove the static type is `NullEventSink`. Minor optimization, but also documents intent: `NullEventSink` is a leaf class.

---

#### `EngineAPI` — the input port

```cpp
class EngineAPI {
public:
    virtual ~EngineAPI() = default;
    virtual EngineResponse submit(const NewOrder& order) = 0;
    virtual EngineResponse cancel(OrderId id) = 0;
    virtual const OrderBook& book() const = 0;
};
```

**Why pure virtual, not a concrete class?**

`EngineAPI` is a pure abstraction — it declares "what the engine can do," not "how it does it." `MatchingEngine` (Task 9) will be the only concrete implementation in Phase 1, but:

- Phase 2's benchmark harness depends on `EngineAPI*`, not `MatchingEngine*`, so it can be reused with a mock or a different engine without touching the harness code.
- A test double (mock engine that records calls) can implement `EngineAPI` and be injected anywhere without modifying production code.
- The CLI (`apps/cli/main.cpp`) depends on `EngineAPI*` — it works without knowing anything about `MatchingEngine`. This is the Dependency Inversion Principle: high-level policy (CLI logic) depends on an abstraction, not on a low-level implementation detail (matching engine internals).

**Why `book()` returns `const OrderBook&`, not a value or a pointer:**

- **Not a value:** `OrderBook` owns all resting orders via `unique_ptr`s. Returning by value would require a deep copy of the entire order book, which is expensive and semantically wrong for a "read-only view."
- **Not a raw pointer:** a raw `OrderBook*` would require the caller to check for null and gives no indication of ownership or lifetime. `const OrderBook&` states clearly: "you can read this, you don't own it, and it's guaranteed non-null."
- **`const`:** the caller (CLI's `PRINT_BOOK`, tests) should not modify the book through this reference — they're observers. `const` enforces that at compile time.

**Why forward-declare `OrderBook` instead of including `orderbook/order_book.hpp`:**

`EngineAPI` only returns `const OrderBook&` from `book()`. For a forward declaration, that's sufficient — a reference to a type doesn't require the full type definition, only the name. Including the full header would:
1. Create a dependency from `interfaces/` on `orderbook/`, which is a layer violation (interfaces should depend only on `core/`, not on `orderbook/`)
2. Force every file that includes `engine_api.hpp` to transitively compile `orderbook/order_book.hpp`

With the forward declaration, users of `EngineAPI` can call `submit()` and `cancel()` without ever knowing `OrderBook` exists. Only code that actually calls `book()` and does something with the result needs to include `orderbook/order_book.hpp` — and that code (the CLI's `PRINT_BOOK` handler) is exactly the right place for that include.

**Complexity:**
- **`EventSink` methods:** O(1) each — pure dispatch, no data structure operations.
- **`NullEventSink::instance()`:** O(1) after first call (just return a pointer).
- **`EngineAPI` methods (as implemented by `MatchingEngine`):** O(log P) for insert, O(1) amortized for cancel — specified fully in `design.md` §7 and implemented in Tasks 9–12.

**Benefits:**
1. **Decoupled evolution:** Tasks 9–14 implement `MatchingEngine` and `apps/cli/` without needing to touch these interfaces. Phase 5's TCP adapter depends only on `EngineAPI` — it will never need to change when Phase 3 replaces the memory allocator.
2. **Testability:** Any test can substitute a `RecordingEventSink` (as done in this task's test file) or a mock `EngineAPI` with zero changes to the engine or CLI.
3. **Observer pattern done right:** `EventSink`'s three methods correspond exactly to three distinct event types. There's no `on_event(EventType, void*)` polymorphism-by-cast hack — each event type has its own strongly-typed method with the correct payload.

**Drawbacks / tradeoffs accepted:**
1. **Virtual dispatch overhead:** Every `sink_->on_trade(t)` call goes through a vtable. In a hot matching loop processing millions of fills per second, that's a real cost (typically one additional indirect branch per call, plus potential instruction-cache pressure). This is accepted for Phase 1 (correctness-first) — Phase 2's benchmarks will quantify it, and Phase 4 may address it via a lock-free event queue that batches vtable calls.
2. **`EngineAPI::book()` couples the engine to `OrderBook`'s public API:** Any future refactoring of `OrderBook` (e.g., Phase 3 switching from `std::map` to a flat array) must also update `book()`'s return type or any code that dereferences it. This is an accepted tradeoff: `book()` is the "read model" for `PRINT_BOOK` and tests, and those uses genuinely need depth-and-queue visibility that only `OrderBook` provides.
3. **No `RecordingEventSink` in the codebase (only in tests):** The `RecordingEventSink` defined in `test_interfaces.cpp` is a test-local class. A future task could promote it to a shared test utility in `tests/test_helpers.hpp` — for now, each test file that needs it defines its own, which is fine at this scale but would be worth extracting if three or more test files end up copy-pasting the same implementation.

**Alternatives considered and rejected:**

1. **`EventSink` with default no-op virtual bodies (as in requirements.md §3 draft):**
   ```cpp
   virtual void on_trade(const Trade&) {}  // not pure
   ```
   - Pros: "selective override" — adapters only override what they care about
   - Cons: silent bugs when method names are misspelled; no compile error for a missing override
   - Rejected: explicit no-ops in each concrete class (one extra line) are a better tradeoff against silent bugs

2. **`std::function<void(const Trade&)>` callbacks instead of a virtual class:**
   ```cpp
   struct EventCallbacks {
       std::function<void(const Trade&)> on_trade;
       // ...
   };
   ```
   - Pros: can inject lambdas without defining a full class
   - Cons: `std::function` has allocation overhead (captures a closure), worse optimizer visibility than virtual dispatch, no clear ownership semantics, harder to implement a "null" default cheaply
   - Rejected: for a system expected to handle millions of events/second, `std::function`'s overhead is too high relative to a plain vtable call, and the "full class" cost is trivial

3. **Template-based static polymorphism (`EventSink` as a template parameter):**
   ```cpp
   template<typename Sink>
   class MatchingEngine {
       Sink* sink_;
       // Sink::on_trade() called directly — no vtable
   };
   ```
   - Pros: zero virtual dispatch overhead; the compiler can inline `NullEventSink`'s empty bodies completely
   - Cons: `MatchingEngine` becomes a template, complicating the `EngineAPI` interface (you can't have a `virtual EngineResponse submit()` and a template parameter at the same time without indirection). Every translation unit that uses `MatchingEngine` must see the full template definition. Phase 2's benchmark harness can't use `EngineAPI*` if the engine is templated.
   - Rejected for Phase 1: correctness and measurability over micro-optimization. If Phase 2's benchmarks reveal vtable overhead is significant, this is a known optimization path.

4. **A single callback instead of three typed methods:**
   ```cpp
   virtual void on_event(const std::variant<Trade, OrderAccepted, OrderCancelled>&) = 0;
   ```
   - Pros: one method to override
   - Cons: requires `std::visit` at every call site, loses type safety (caller could forget to handle one variant), more complex than just "here are the three events you care about"
   - Rejected: three typed methods are simpler, more self-documenting, and less error-prone

**How this connects to what came before:**
- Tasks 2–5 built all the domain types (`OrderId`, `Price`, `Trade`, `OrderAccepted`, `OrderCancelled`, `EngineResponse`, `NewOrder`) that the interfaces reference. `EventSink` and `EngineAPI` are purely the *wiring points* — they bring nothing new conceptually, they're just the sockets into which concrete implementations plug.
- Task 7 (`PriceLevel`) and Task 8 (`OrderBook`) implement the data structures that `EngineAPI::book()` will expose.
- Tasks 9–12 implement `MatchingEngine` — the concrete `EngineAPI` that calls `EventSink`. Those tasks are where the interfaces defined here actually "run" for the first time.

**Check your understanding:**
1. Why does `NullEventSink::instance()` return `NullEventSink*` rather than `EventSink*`? What would change if it returned `EventSink*`? (Hint: think about whether callers could distinguish the null sink from other sinks, and whether `final` + the returned type affect devirtualization.)
2. `EngineAPI` forward-declares `OrderBook` rather than `#include`-ing it. Write out the compilation consequences if you changed the forward declaration to a full include — which files would be forced to recompile when `OrderBook` changes, and why does that violate the layer boundaries?
3. A Phase 6 UDP market-data adapter needs to see every trade. It doesn't submit orders — it only listens. Which of the two channels (`EngineResponse` or `EventSink`) should it use, and why? Could it use the other one, and what would that require?
4. The `RecordingEventSink` in the test file is redefined in each test file that needs it. At what point would you extract it into a shared `tests/test_helpers.hpp`, and what signal would tell you it's time?

### Task 7 — `orderbook/PriceLevel.hpp` / `.cpp`

**What it does:**
Implements the FIFO queue of resting orders at a single price point. Each `PriceLevel` manages an intrusive doubly-linked list of `Order*` pointers, maintaining aggregate quantity incrementally. This is the building block that `OrderBook` (Task 8) composes into a full two-sided price tree.

**Exact locations:**
- `orderbook/price_level.hpp` (lines 1–53) — class declaration
- `orderbook/price_level.cpp` (lines 1–56) — implementation of `push_back`, `remove`, `front`, `empty`, `price`, `total_quantity`
- `tests/price_level_test.cpp` — comprehensive test suite

**Why this data structure, specifically:**

A price level is conceptually a FIFO queue: orders at the same price fill in the order they arrived (price-time priority). The implementation choice is an **intrusive doubly-linked list** — "intrusive" meaning the `prev`/`next` pointers live inside the `Order` struct itself (defined in Task 4), not in a separate node allocation.

The `PriceLevel` class encapsulates all the pointer-manipulation logic that makes intrusive lists work:

```cpp
void PriceLevel::push_back(Order* order) {
    order->level = this;         // set back-pointer
    order->prev = tail_;
    order->next = nullptr;
    if (tail_) tail_->next = order;
    else head_ = order;
    tail_ = order;
    total_qty_ += order->quantity;
}
```

This is the "5 lines of pointer fixups" mentioned in Task 4's docs. The point of `PriceLevel` is to contain this complexity in one place — callers (the `OrderBook`) simply call `level.push_back(order)` and don't deal with raw pointer manipulation.

**Why `total_quantity_` is maintained incrementally:**

Alternative: traverse the list and sum `order->quantity` for every `total_quantity()` call. Cost: O(n) per read, where n = orders at this level.

Chosen: maintain a running sum, updated in O(1) on every `push_back` (add) and `remove` (subtract). Cost: O(1) per read, at the expense of 8 bytes storage and the discipline of never forgetting to update it.

This matters because `total_quantity()` is read frequently: every `PRINT_BOOK` command in the CLI, every market-data depth update in Phase 6. O(n) per read would be unacceptable for a deep level with hundreds of resting orders.

**Complexity:**
- **`push_back`:** O(1) — tail append, constant-time quantity add
- **`remove`:** O(1) — unlink via `order->prev`/`order->next`, constant-time quantity subtract
- **`front`:** O(1) — return `head_`
- **`empty`:** O(1) — check `head_ == nullptr`
- **`total_quantity`:** O(1) — return stored value

**Benefits:**
1. All operations O(1) — no operation on a PriceLevel scales with the number of orders at that price
2. Zero allocation — PriceLevel doesn't allocate memory; it just links/unlinks Order pointers that are owned elsewhere (OrderBook's `unordered_map`)
3. Self-contained invariants — all pointer fixups happen in exactly two methods (`push_back`, `remove`), making bugs easy to locate

**Drawbacks / tradeoffs:**
1. **PriceLevel doesn't own orders.** If the owning `unique_ptr` is destroyed before `remove` is called, the list contains a dangling pointer. This is safe because `OrderBook::remove_order` always calls `level->remove(order)` before erasing from the owning map — but if someone broke that sequence, it would be a use-after-free. The coupling is by convention, not enforced by the type system.
2. **Can't iterate safely while modifying.** Calling `remove` on the current order during iteration invalidates the pointer. The matching loop (Task 10) handles this by capturing `front()`, consuming it, then re-reading `front()` for the next iteration.

**Alternatives considered and rejected:**
1. **`std::deque<Order*>`** — O(1) push_back and front access, but O(n) remove-from-middle (needed for cancel). Rejected.
2. **`std::list<Order*>`** — O(1) insert/remove via iterators, but requires a separate heap allocation per node *in addition to* the Order itself. Rejected per steering hard rule.
3. **Circular buffer** — O(1) push/pop from ends, but remove-from-middle is O(n) (shift elements). Rejected for same reason as deque.

**How this connects to what came before:**
- Task 4 defined `Order` with `prev`/`next`/`level` fields — PriceLevel is where those fields are actually used
- Task 8 (OrderBook) will create and destroy PriceLevels as orders arrive at new prices or leave empty prices

**Check your understanding:**
1. Why does `remove` clear `order->prev`, `order->next`, and `order->level` to nullptr after unlinking? What bug would occur if it didn't?
2. If two orders at the same price have quantities 30 and 70, what does `total_quantity()` return? What happens to this value if the first order's quantity is reduced (partial fill) but `PriceLevel` isn't informed?


### Task 8 — `orderbook/OrderBook.hpp` / `.cpp`

**What it does:**
Implements the central data structure that holds all resting orders in the matching engine. `OrderBook` maintains two price trees (bids and asks), each containing `PriceLevel` instances, plus an ownership map (`unordered_map<OrderId, unique_ptr<Order>>`) that is the single source of truth for order lifetime. It provides the structural primitives (`add_order`, `remove_order`, `find_order`, `best_bid`, `best_ask`) that the engine (Task 9+) will compose into matching logic.

**Exact locations:**
- `orderbook/order_book.hpp` (lines 1–72) — class declaration with type aliases and method signatures
- `orderbook/order_book.cpp` (lines 1–72) — implementation
- `tests/order_book_test.cpp` (lines 1–180) — 15 GoogleTest cases covering all acceptance criteria

**Why this data structure, specifically:**

The OrderBook uses three cooperating data structures:

1. **`std::map<Price, PriceLevel, std::greater<Price>> bids_`** — a red-black tree sorted by price *descending*. `begin()` is the highest bid (best bid). Using `std::greater` as the comparator flips the default ascending sort to descending.

2. **`std::map<Price, PriceLevel, std::less<Price>> asks_`** — a red-black tree sorted by price *ascending* (the default). `begin()` is the lowest ask (best ask).

3. **`std::unordered_map<OrderId, std::unique_ptr<Order>> orders_`** — the owning index. Every resting order's lifetime is managed here. The `PriceLevel` intrusive lists hold raw, non-owning `Order*` into these same objects.

**Why two separate maps instead of one with a conditional comparator:**

Alternative: a single `std::map<Price, PriceLevel, ConditionalComparator>` that sorts differently based on side. This is impractical — a `std::map`'s comparator is a template parameter, fixed at compile time. You can't have one map instance that sorts differently for different elements.

Alternative: one `std::map` for all prices (ascending), then `best_bid()` uses `rbegin()` and `best_ask()` uses `begin()`. This works but has a subtle problem: iterating "through the ask side" means iterating forward from `begin()` until you hit bid prices, requiring a side-check at each level. With two separate maps, each side is self-contained — `bids_.begin()` is always the best bid, `asks_.begin()` is always the best ask, no branching needed.

The chosen design (two maps) is simpler to reason about and avoids per-level side-checking during iteration.

**Why `std::map` specifically (vs. alternatives):**

`std::map` is a red-black tree — self-balancing BST with O(log P) insert, O(log P) erase, and O(1) `begin()` (the leftmost node pointer is cached by libstdc++/libc++ implementations).

For Phase 1, `std::map` is explicitly acceptable (see `tech.md`: "Phase 1 is correctness-first; `std::map` for the price tree is fine"). Phase 2 will benchmark it, and Phase 3 may replace it with a more cache-friendly structure (flat sorted array, skip list, or direct-mapped price array for instruments with bounded price ranges). The key insight: `std::map` is *correct* and *simple* — it just might not be the *fastest* option for cache-line-sized price levels in a latency-sensitive matching loop.

**Why `std::greater<Price>` for bids:**

The matching engine's inner loop walks bids from best (highest) to worst (lowest). With `std::less` (default), best bid is at `rbegin()` — a reverse iterator. Reverse iterators have subtle gotchas (they point to the element *before* their base iterator, which confuses debugging), and they're not directly comparable with forward iterators returned by `find`/`lower_bound`.

With `std::greater`, best bid is at `begin()` — a normal forward iterator. The matching loop can iterate `for (auto it = bids_.begin(); ...)` naturally, moving from highest price to lowest. This is a pure ergonomics choice — no performance difference, just fewer "which direction am I going?" bugs.

**Why `unordered_map<OrderId, unique_ptr<Order>>` for ownership:**

This map serves two roles:
1. **Lifetime management:** `unique_ptr<Order>` ensures orders are destroyed exactly once, when `remove_order` erases them from this map. No manual `delete`, no double-free, no leak.
2. **O(1) lookup by ID:** When the engine needs to find or cancel an order, it looks up by `OrderId` in O(1) amortized time. Without this, finding "order 42" would require scanning all price levels (O(n) where n = total resting orders).

The alternative (only use the price tree + intrusive lists, no separate ID index) would make cancel O(n) — walk every level on both sides looking for the order. The charter requires O(1) cancel. The `unordered_map` is the mechanism that achieves it.

**Why `add_order` takes `unique_ptr<Order>` (ownership transfer):**

```cpp
Order* OrderBook::add_order(std::unique_ptr<Order> order);
```

The caller (engine) creates the `Order`, assigns its sequence number, and then *transfers ownership* to the book via `std::move`. After this call, the engine holds only a raw `Order*` (returned for immediate use, e.g., linking into the PriceLevel). The book exclusively owns the order's lifetime.

This ownership model is explicit and unambiguous — there's no question about "who deletes this order?" The answer is always: the book, when `remove_order` is called.

**Why `remove_order` takes `Order*` (not `OrderId`):**

```cpp
void OrderBook::remove_order(Order* order);
```

The engine already has the `Order*` (from `find_order` or from the matching loop traversing the PriceLevel). Passing the raw pointer avoids a redundant `unordered_map` lookup. Inside `remove_order`:
1. Unlink from PriceLevel via `level->remove(order)` — O(1)
2. Check if level is now empty → erase from price tree — O(log P)
3. Erase from owning map via `orders_.erase(order->id)` — O(1) amortized

This destroys the Order (the `unique_ptr` is erased). After `remove_order` returns, the `Order*` is dangling — callers must not use it.

**Why `order_count_` is maintained incrementally:**

Alternative: `return orders_.size()` (the `unordered_map` already tracks its own size). This would work, but I chose an explicit counter for two reasons:
1. Clarity of intent — `order_count_` documents that "the book knows how many orders it has" as a first-class concern
2. Future flexibility — if Phase 3 replaces `unordered_map` with a pool + free-list, `.size()` might not be available or might be expensive. An explicit counter is always O(1)

In practice, `orders_.size()` would be equivalent and arguably simpler. This is a minor design choice that could go either way.

**Why `find_order` returns `Order*` (raw, non-owning):**

The caller needs to inspect or modify the order (read its quantity during matching, update `quantity` on partial fill). It does NOT need to own it — ownership stays in the `unordered_map`. Returning `const unique_ptr<Order>&` would expose the ownership mechanism to callers who shouldn't care about it. Raw `Order*` says: "here's the object, you can use it, you don't own it."

**Why this architecture:**

`OrderBook` lives in `orderbook/`, not `engine/`. It's a **data structure**, not business logic. It knows how to:
- Insert an order into the correct side/level
- Remove an order and clean up empty levels
- Report top-of-book

It does NOT know:
- Whether an order should be accepted (validation — engine's job)
- Whether an incoming order crosses the book (matching — engine's job)
- What events to emit (EventSink — engine's job)

This separation means the engine can compose `OrderBook` primitives into different algorithms without OrderBook needing to change. If Phase 8's risk engine needs to check "how many lots does this trader have resting?" — it queries the OrderBook, which just answers. It doesn't need to understand *why* the question is being asked.

**Complexity:**
- **`add_order`:** O(log P) worst case (creating a new price level via `std::map::try_emplace`). O(1) amortized if the level already exists (hash map insert + intrusive list append).
- **`remove_order`:** O(log P) worst case (erasing an empty level from the tree). O(1) amortized for the unlink + hash map erase.
- **`find_order`:** O(1) amortized — `unordered_map::find`.
- **`best_bid` / `best_ask`:** O(1) — `std::map::begin()` returns the cached leftmost node pointer.
- **`order_count`:** O(1) — return stored counter.

**Benefits:**
1. **Clean separation of data structure from business logic.** OrderBook is testable in isolation (as demonstrated by the 15 tests that never reference EngineAPI or EventSink).
2. **O(1) cancel** via the `unordered_map` lookup + intrusive unlink. The charter's complexity target is met.
3. **O(1) top-of-book** via `std::map::begin()`. The matching loop starts here every time.
4. **Ownership is unambiguous.** `unique_ptr` in the map, raw pointers everywhere else. No shared ownership, no reference counting overhead.
5. **Level pruning is automatic.** `remove_order` checks if the level is empty and erases it. The book never has phantom empty levels cluttering the tree.

**Drawbacks / tradeoffs accepted:**
1. **`std::map` is not cache-friendly.** Each node in a red-black tree is a separate heap allocation. Traversing the tree chases pointers, causing cache misses. For Phase 1 (correctness-first), this is fine. Phase 2 will benchmark, Phase 3 may replace with a flat sorted array or pool-allocated tree nodes.
2. **`unordered_map` has high per-element overhead.** Each bucket is a linked list (in libstdc++), so iterating all orders is cache-unfriendly. For lookup-by-ID (the primary use case), amortized O(1) is what matters. If bulk iteration becomes a bottleneck (Phase 6 market data), this can be revisited.
3. **`remove_order` destroys the Order immediately.** If the engine holds a dangling `Order*` after calling `remove_order`, that's undefined behavior. The engine must be careful not to use the pointer after removal. This is a correctness discipline issue, not a runtime-detectable error (though valgrind/ASan would catch it in testing).
4. **`try_emplace` constructs a PriceLevel even if the level already exists... no, actually `try_emplace` only constructs if the key is absent.** This is correct — `try_emplace(price, price)` only calls `PriceLevel(price)` if no level exists at that price. If the level exists, it's a no-op. This is the correct behavior.

**Alternatives considered and rejected:**

1. **Single `std::map` for both sides, with side stored in the key:**
   ```cpp
   std::map<std::pair<Side, Price>, PriceLevel> levels_;
   ```
   - Pros: One container instead of two
   - Cons: `best_bid()` requires finding the last Buy entry (reverse iteration with a side check), `best_ask()` requires finding the first Sell entry. More complex, more branching, no performance benefit.
   - Rejected for simplicity.

2. **`std::unordered_map<Price, PriceLevel>` instead of `std::map`:**
   - Pros: O(1) insert/lookup of a specific price level
   - Cons: Cannot find best bid/ask without scanning all levels (no ordering). Hash maps are unordered by definition.
   - Rejected because top-of-book query is critical.

3. **Sorted `std::vector<PriceLevel>` (flat sorted array):**
   - Pros: Cache-friendly iteration (contiguous memory), binary search for lookup
   - Cons: O(n) insert (shift elements) and O(n) erase (shift elements). For a book with 1000 price levels, inserting a new level in the middle shifts 500 elements.
   - Rejected for Phase 1 (O(n) insert is unacceptable). May be reconsidered in Phase 3 if benchmarks show cache dominates.

4. **Skip list:**
   - Pros: O(log P) insert/remove/search (like `std::map`) but cache-friendlier (fewer pointer chases per operation on average)
   - Cons: More complex implementation, no standard library support, harder to debug
   - Rejected for Phase 1 (correctness-first, use standard containers). Worth evaluating in Phase 3.

5. **OrderBook owning PriceLevels by pointer (`map<Price, unique_ptr<PriceLevel>>`):**
   - Pros: Levels don't move in memory when the map rebalances (important if external code holds PriceLevel pointers)
   - Cons: Extra indirection (pointer chase through the map to the level, then through the level to orders). Extra allocation per level.
   - Rejected because `std::map` already stores values by node — `map<Price, PriceLevel>` values don't move when the tree rebalances (they're allocated as part of the tree node). No extra indirection needed.

**How this connects to what came before:**
- Task 4 (`Order`) defined the intrusive list pointers and the `level` back-pointer — OrderBook is the consumer of these fields (via `add_order` linking into PriceLevel, and `remove_order` using the back-pointer for O(1) unlink)
- Task 7 (`PriceLevel`) implemented the per-level FIFO queue — OrderBook creates/destroys PriceLevels and delegates order linking/unlinking to them
- Task 9 (next) will implement `MatchingEngine`, which uses `OrderBook` as its core data structure — calling `add_order` to rest orders, `remove_order` to clean up filled orders, `best_bid`/`best_ask` to find the top of book for matching

**Check your understanding:**
1. Why does `bids_` use `std::greater<Price>` as its comparator? What would `best_bid()` look like if it used the default `std::less<Price>` comparator instead?
2. If you insert two orders at the same price, they end up in the same `PriceLevel`. Where is the FIFO guarantee enforced — in `OrderBook::add_order`, in `PriceLevel::push_back`, or both?
3. After `remove_order(order)` returns, the `Order*` pointer is dangling. Why is this safe in practice? What mechanism prevents the engine from accidentally using the pointer after removal?
4. Why does `OrderBook` have *both* a `std::map` (price tree) *and* an `unordered_map` (ID index)? Could you achieve O(1) cancel with only the `std::map`? What would the complexity be?
5. `try_emplace(price, price)` — the first `price` is the map key, the second is the constructor argument for `PriceLevel`. Why not `emplace(price, PriceLevel{price})`? What's the difference in behavior when the key already exists?


### Task 9 — `engine/MatchingEngine.hpp` / `.cpp` — Limit-Order Submission + Matching

**What it does:**
Implements the `MatchingEngine` class — the concrete implementation of the `EngineAPI` input port. This is where all the structural primitives from Tasks 2–8 come together into actual trading logic. The engine validates incoming limit orders, performs price-time priority matching against the opposite side of the book, emits events to the injected `EventSink`, and returns structured results to the caller. It's the heart of MiniExchange: everything below it (`orderbook/`, `core/`) is data structure; everything above it (`apps/`, `adapters/`) is I/O.

**Exact locations:**
- `engine/matching_engine.hpp` (lines 1–65) — class declaration, member variables, private helper signatures
- `engine/matching_engine.cpp` (lines 1–130) — full implementation: `submit`, `submit_limit`, `match_against_book`
- `tests/matching_engine_test.cpp` (lines 1–250) — 23 GoogleTest cases covering validation, acceptance events, crossing, partial fills, multi-level sweeps, FIFO, and sequence counters

---

#### Class Shape and Constructor

```cpp
class MatchingEngine : public EngineAPI {
public:
    explicit MatchingEngine(EventSink* sink = NullEventSink::instance());
    EngineResponse submit(const NewOrder& order) override;
    EngineResponse cancel(OrderId id) override;
    const OrderBook& book() const override;

private:
    OrderBook book_;
    std::unordered_set<OrderId> ever_seen_ids_;
    Sequence next_sequence_{0};
    TradeSequence next_trade_sequence_{0};
    EventSink* sink_;

    EngineResponse submit_limit(const LimitOrder& order);
    EngineResponse submit_market(const MarketOrder& order);
    std::vector<Trade> match_against_book(
        Side incoming_side, OrderId incoming_id,
        Quantity& remaining, std::optional<Price> limit_price);
};
```

The constructor takes an `EventSink*` defaulting to the `NullEventSink` singleton. This is constructor-based dependency injection — the engine doesn't know or care whether it's being observed by a unit test's `RecordingEventSink`, a CLI printer, a UDP multicast publisher, or nothing at all. It just calls through the pointer. The `NullEventSink` default means tests that only care about `EngineResponse` don't need to wire up a sink.

**Why `EventSink*` (raw pointer), not `unique_ptr` or `shared_ptr`:**
The engine doesn't *own* the sink. The sink's lifetime is managed by whoever wired it up (the `main()` function, the test fixture). The engine just holds a non-owning pointer to it. `unique_ptr` would imply ownership transfer (wrong); `shared_ptr` would add reference-counting overhead on every event emission (unnecessary). A raw pointer with "engine does not own this" documentation is the correct tool here — the same pattern used by Observer implementations everywhere in systems code.

---

#### `submit()` — Variant Dispatch

```cpp
EngineResponse MatchingEngine::submit(const NewOrder& order) {
    return std::visit(
        [this](const auto& o) -> EngineResponse {
            if constexpr (std::is_same_v<std::decay_t<decltype(o)>, LimitOrder>) {
                return submit_limit(o);
            } else {
                return submit_market(o);
            }
        },
        order);
}
```

`std::visit` with a generic lambda plus `if constexpr` is the idiomatic C++17/20 pattern for dispatching on a variant's active alternative. The compiler generates two instantiations of the lambda body — one for `LimitOrder`, one for `MarketOrder` — and inserts a branch (or jump table) to select the right one at runtime based on the variant's discriminator.

**Why not a `switch` on an enum:**
There's no enum to switch on — `NewOrder` is `std::variant<LimitOrder, MarketOrder>`, and the "which type?" information is encoded in the variant's internal index. `std::visit` is the type-safe accessor. If we later add `StopOrder` to the variant, any `visit` that doesn't handle it will fail to compile — you can't forget a case.

**Why `if constexpr` inside the lambda rather than an overload set:**
Alternative pattern uses a "deduction guide" overload struct:
```cpp
struct Visitor {
    EngineResponse operator()(const LimitOrder& o) { return submit_limit(o); }
    EngineResponse operator()(const MarketOrder& o) { return submit_market(o); }
};
return std::visit(Visitor{}, order);
```
This is equivalent. The `if constexpr` + generic lambda approach was chosen because it's more concise for two alternatives and keeps the dispatch logic in one place (inside `submit()`). For 5+ alternatives, a named overload struct would be cleaner.

---

#### `submit_limit()` — Validation, Acceptance, Matching, Resting

This is the main entry point for limit order processing. The flow is:

1. **Validate input** — reject invalid orders before any side effects occur
2. **Accept** — record the ID, emit `on_order_accepted`
3. **Match** — run the matching loop against the opposite side
4. **Rest remainder** — if anything is left after matching, put it on the book

```cpp
EngineResponse MatchingEngine::submit_limit(const LimitOrder& order) {
    // 1. Validation
    if (order.quantity == Quantity{0})
        return EngineResponse{EngineResult::InvalidQuantity, {}, Quantity{0}};
    if (order.price <= Price{0})
        return EngineResponse{EngineResult::InvalidPrice, {}, Quantity{0}};
    if (ever_seen_ids_.contains(order.id))
        return EngineResponse{EngineResult::DuplicateOrderId, {}, Quantity{0}};

    // 2. Accept
    ever_seen_ids_.insert(order.id);
    sink_->on_order_accepted(OrderAccepted{order.id, order.side, order.quantity});

    // 3. Match
    Quantity remaining = order.quantity;
    std::vector<Trade> trades = match_against_book(
        order.side, order.id, remaining, order.price);

    // 4. Rest remainder
    if (remaining > Quantity{0}) {
        auto resting = std::make_unique<Order>(Order{...});
        book_.add_order(std::move(resting));
    }

    return EngineResponse{EngineResult::Accepted, std::move(trades), remaining};
}
```

**Validation order matters:** quantity is checked first (cheapest check), then price, then duplicate-ID (requires a hash lookup). Early return on validation failure means we never emit events or modify state for invalid input — this satisfies R19 ("rejections don't emit events").

**Why `ever_seen_ids_` is separate from `OrderBook::orders_`:**

The `OrderBook::orders_` map only contains *currently resting* orders. An order that was fully filled is removed from that map. But the engine must reject a reuse of that order's ID forever (lifetime-unique per requirements §2.1). So `ever_seen_ids_` tracks "every ID ever accepted, regardless of current state." It only grows (IDs are never removed), which is the correct behavior for a lifetime-uniqueness check.

Alternative: store a separate "filled/cancelled" set and check both `orders_` and that set. This is more complex (two lookups) for no benefit — one set that covers everything is simpler.

**Why `on_order_accepted` is emitted BEFORE matching:**

The requirements say "emitted when the engine accepts the order" (R16). An order is "accepted" the moment its ID is recorded and the engine takes responsibility for it — even if matching is about to fill it completely. A market order that fills immediately is still "accepted" first, then filled. This ordering is important for market-data feed consumers (Phase 6): they see `OrderAccepted` followed by `Trade(s)`, which lets them reconstruct the book state at any point in time without ambiguity.

---

#### `match_against_book()` — The Core Algorithm

```cpp
std::vector<Trade> MatchingEngine::match_against_book(
    Side incoming_side, OrderId incoming_id,
    Quantity& remaining, std::optional<Price> limit_price);
```

This is the shared matching loop, parameterized to work for both limit and market orders. The algorithm:

```
while remaining > 0:
    level = opposite_side.best_level()
    if level is null: break                           // no liquidity
    if limit_price set and doesn't cross: break       // R5: price barrier
    while remaining > 0 and level not empty:
        resting = level.front()                       // FIFO: oldest first
        fill_qty = min(remaining, resting.quantity)
        trade = Trade{next_trade_sequence_++, buy_id, sell_id,
                      resting.price, fill_qty}        // R6: at resting price
        trades.push_back(trade)
        sink_->on_trade(trade)                        // R17: per-fill emission
        remaining -= fill_qty
        resting.quantity -= fill_qty
        if resting.quantity == 0:
            book_.remove_order(resting)               // R7: fully consumed
```

**Key design decisions in this loop:**

1. **`std::optional<Price> limit_price`:** For limit orders, this is the order's price. For market orders (Task 11), this is `std::nullopt` — meaning "no price limit, match at any available price." One parameter, two behaviors.

2. **Crossing condition (R5):** A buy crosses if its limit price ≥ the best ask price. A sell crosses if its limit price ≤ the best bid price. If the limit price is `nullopt` (market order), crossing is always true (no barrier).

3. **Trade price is always the resting order's price (R6):** Not the incoming order's price, not the midpoint, not an average. This is price-time priority: the resting order "offered" at its price, and the incoming order "accepted" that offer. If an incoming buy at 102 sweeps through asks at 100, 101, 102, it gets three trades at 100, 101, and 102 respectively — each at the resting level's price.

4. **`remaining` is passed by reference and modified in-place:** This is a deliberate design choice. The caller (`submit_limit`) passes its local `remaining` variable, and `match_against_book` decrements it as fills occur. After the loop, the caller inspects `remaining` to decide whether to rest the order. This avoids returning a `{trades, remaining}` tuple — the trades are returned via the function return value, the remaining is an output parameter. Cleaner than a struct return for this specific two-output case.

5. **Per-fill `on_trade` emission (R17, R20):** Each individual fill emits a separate `on_trade` call *during* the matching loop, not batched after. This means observers see trades in real-time order — if the engine crashes mid-match (unlikely in production but possible during testing), the already-emitted trades are still consistent. This also satisfies R20's "synchronous emission before EngineResponse returns."

6. **`book_.remove_order(resting)` for fully-consumed orders (R7):** When a resting order's quantity hits zero, it's removed immediately. This triggers `PriceLevel::remove` (unlink from intrusive list) and, if the level becomes empty, removal from the price tree. The next iteration of the inner while loop will then see `level->empty()` or get a new `level->front()`.

**Why one shared loop for both limit and market:**

The matching algorithm is fundamentally identical for both order types — the only difference is:
- Limit: has a price barrier (stop matching when opposite side's price doesn't cross)
- Market: has no price barrier (match everything available)

Having two separate loops (`match_limit_against_book` and `match_market_against_book`) with 90% duplicate code would be a maintenance hazard — a bug fix in one might not be applied to the other, and the code would drift. One parameterized loop (the `limit_price` optional) is DRY and correct.

---

#### Sequence Counters

**`next_sequence_`:** Assigned to each order when it's placed on the book. Used for FIFO tiebreaking — at a given price level, orders are filled in `sequence` order (lower = earlier = fills first). The counter starts at 0 and increments monotonically. It's only assigned to orders that actually *rest* (i.e., have remaining quantity after matching), not to orders that fill completely on arrival.

**`next_trade_sequence_`:** Assigned to each `Trade` as it's generated. Monotonically increasing across all trades regardless of which order triggered them. Used by market-data feeds (Phase 6) for gap detection and by replay systems for deterministic sequencing.

**Why not one shared counter for both:** Sequences and trade sequences serve different consumers and increment at different rates. In a high-volume scenario, there might be 10 trades per order (one aggressive sweep), or 0 trades per order (resting order). Sharing a counter would make gaps in the trade sequence (from resting orders that didn't trade) confusing for market-data consumers expecting contiguous trade numbers.

---

#### `EngineResponse` Return Value

Every call to `submit_limit` returns:
- `status`: `EngineResult::Accepted` (all non-validation cases) or a rejection code
- `trades`: vector of all fills that occurred (empty if no crossing)
- `remaining_qty`: how much of the incoming order is left after matching (0 if fully filled, or the rested quantity if partially filled and placed on book)

This is the *synchronous, per-caller* response. The caller gets everything it needs to know about its own order in one return value, without needing to register as a listener.

---

**Why this architecture:**

`MatchingEngine` lives in `engine/`, depends on `orderbook/` and `core/`, and is depended upon by `apps/` and `adapters/` (via the `EngineAPI` interface, never directly). Key architectural properties:

1. **Zero I/O:** No `printf`, no `cout`, no file writes, no socket calls. The engine is a pure function of its inputs (orders) producing outputs (responses + events). All presentation, logging, and network activity is the caller's responsibility.

2. **No exceptions for business outcomes:** Duplicate ID, invalid price/quantity — these are returned as `EngineResult` status codes, not thrown. Exceptions are reserved for programming errors (invariant violations). This is how real exchange engines work: processing millions of orders per second, a thrown exception per invalid order would destroy performance and complicate stack unwinding in the hot path.

3. **Deterministic:** Given the same sequence of inputs, the engine produces the same sequence of outputs. No wall-clock time reads, no random numbers, no external state. This makes replay and testing trivial.

4. **Testable in isolation:** The test fixture creates a `MatchingEngine` with a `RecordingEventSink`, submits orders, and asserts on the response and captured events. No mocks needed for dependencies (the `OrderBook` is a real one, not a mock), because the dependencies are deterministic data structures with no I/O of their own.

**Complexity:**
- **`submit_limit` (no crossing):** O(1) for validation + O(1) for `ever_seen_ids_.insert` + O(log P) for `book_.add_order` (new price level creation). **Total: O(log P).**
- **`submit_limit` (with crossing):** O(k) where k = number of resting orders consumed, plus O(log P) per empty level removed from the tree. **Total: O(k + levels_consumed × log P).** In practice, most aggressive orders consume 1–3 levels, so this is effectively O(k).
- **`cancel` (Task 12):** O(1) amortized — hash lookup + intrusive unlink + potential O(log P) level removal.
- **Space:** `ever_seen_ids_` grows linearly with total orders ever submitted (O(N) where N = lifetime order count). In a real system with millions of orders per day, this would eventually need a bounded structure (e.g., time-based expiry). For Phase 1, unbounded growth is acceptable.

**Benefits:**
1. **Correctness by composition:** The engine doesn't reimplement anything — it calls `OrderBook::add_order`, `PriceLevel::front`, `OrderBook::remove_order`. Each primitive is independently tested (Tasks 7–8). The engine's tests verify the *composition* works correctly.
2. **Single responsibility:** The engine does matching logic only. Book structure is `orderbook/`'s job, type definitions are `core/`'s job, I/O is `apps/`'s job.
3. **Two output channels cleanly separated:** `EngineResponse` (synchronous return to caller) and `EventSink` (broadcast to observers) never interfere. A caller that doesn't implement `EventSink` still gets its response. An observer that didn't call `submit` still sees all trades.
4. **Parameterized matching loop:** One well-tested algorithm serves both limit and market orders, reducing duplication and drift risk.

**Drawbacks / tradeoffs accepted:**
1. **`ever_seen_ids_` unbounded growth:** In a long-running system, this set grows forever. For a portfolio project demonstrating matching logic, this is fine. A production system would cap it or use time-based rotation (e.g., IDs expire after market close). Not worth adding that complexity in Phase 1.
2. **`std::make_unique<Order>` allocation on every resting order:** Every order that rests requires a heap allocation. Phase 3's memory pool will replace this with O(1) pool allocation from a pre-allocated slab. For now, `make_unique` is correct and simple.
3. **Virtual dispatch overhead on `sink_->on_trade()`:** Each fill pays for a vtable lookup. In the hot matching loop, this adds latency. Acceptable for Phase 1; Phase 2 will quantify it.
4. **`remaining` as an output parameter (reference) rather than a return value:** Some developers find output parameters harder to reason about than return values. The alternative (return a struct `{vector<Trade>, Quantity}`) would require an extra struct definition for one call site. The ref parameter is a pragmatic choice for this internal helper.

**Alternatives considered and rejected:**

1. **Event-sourced architecture (record events, replay from log):**
   - Emit events instead of modifying state directly; reconstruct book state by replaying the event log
   - Pros: Built-in audit trail, easy replay, time-travel debugging
   - Cons: Dramatically more complex for Phase 1. Every read operation must replay or cache state. Latency increases.
   - Rejected: correctness-first means direct state manipulation. Event sourcing may come in Phase 7's replay app, but the engine itself won't be event-sourced.

2. **Returning `std::expected<EngineResponse, EngineError>` instead of status codes inside `EngineResponse`:**
   - Pros: Clearer "success/failure" separation (C++23's `std::expected` or Boost.Outcome)
   - Cons: `std::expected` is C++23, not C++20. Boost adds a dependency. And `EngineResponse` with an `Accepted` status that includes trades is genuinely *not* an error — even a partially-filled order is a success. The line between "error" and "different kind of success" isn't clean here.
   - Rejected for simplicity: one `EngineResponse` type with a status enum covers all cases without an extra error type.

3. **Separate `submit_limit_no_match` and `submit_limit_with_match` functions:**
   - Pros: Simpler control flow per function
   - Cons: The "no match" case is just the "with match" case where the while loop doesn't execute (no crossing). Splitting creates two code paths that must be kept in sync for validation and event emission.
   - Rejected: one function with a conditional matching loop is simpler.

4. **Emit `on_order_accepted` AFTER matching (only for orders that rest):**
   - Pros: Market-data consumers wouldn't see "accepted" followed immediately by "fully filled" — they'd only see accepted for orders that actually rest
   - Cons: Violates requirements R16 ("accepted" means the engine took the order, regardless of outcome). A fully-filled aggressive order was still accepted — it just didn't rest. The accepted event is about *validation passing*, not about *resting*.
   - Rejected per requirements R16.

**How this connects to what came before:**
- Tasks 2–5 created the type vocabulary (`OrderId`, `Price`, `Trade`, `EngineResponse`, `NewOrder`)
- Task 6 created the ports (`EngineAPI`, `EventSink`) — `MatchingEngine` is the concrete implementation
- Tasks 7–8 created the data structures (`PriceLevel`, `OrderBook`) — `MatchingEngine` composes them into matching logic
- Task 10 (next) will implement `submit_market` using the same `match_against_book` loop
- Task 12 will implement `cancel()`, completing the engine's public API

**Check your understanding:**
1. Why does `match_against_book` take `std::optional<Price> limit_price` rather than a separate boolean "is this a market order?" flag? What would the code look like with a boolean, and why is the optional cleaner?
2. A limit buy at price 102 arrives. The book has asks at 100 (qty 10), 101 (qty 15), and 103 (qty 20). How many trades are generated, what are their prices and quantities, and what is `remaining_qty` in the response?
3. Why is `on_order_accepted` emitted *before* matching begins, even though the order might be fully filled milliseconds later? What problem would it cause for market-data consumers if "accepted" came after the trades?
4. `ever_seen_ids_` is an `unordered_set` that only grows. In a system processing 1 million orders per day, how large would this set be after a year? Why is this acceptable for a portfolio project but not for a production exchange?
5. The engine modifies `resting->quantity` directly during matching (`resting->quantity -= fill_qty`). What would happen if, instead of modifying the resting order in-place, we removed it and re-inserted with a new quantity? What's the performance difference?


### Task 10 — Additional FIFO / Price-Time Priority Tests

**What it does:**
Adds deeper verification of the matching engine's price-time priority behavior — the property that makes a limit order book an *ordered* matching system rather than a random one. Task 9 already proved the basic matching loop works (one order crosses one order, one order crosses multiple levels). Task 10 verifies the *ordering guarantees* hold under stress: correct FIFO within a level when multiple orders are partially consumed, correct level-sweep order even when orders are inserted in non-price order, and consistency between the two output channels (`EngineResponse.trades` and `EventSink::on_trade` calls).

**Exact locations:**
- `tests/matching_engine_test.cpp` — six new test cases added after the existing Task 9 tests:
  - `FIFOPartialFillThirdOrderAtSamePrice` (lines ~250–295)
  - `TradeSequenceStrictlyIncreasingWithinSubmission` (lines ~297–314)
  - `EventSinkOnTradeOrderMatchesEngineResponse` (lines ~316–344)
  - `PriceTimePrioritySweepsLevelsInOrder` (lines ~346–381)
  - `FIFOWithinLevelRespectedForMultipleOrders` (lines ~383–422)
  - `SelfCrossingMatchesNormally` (lines ~424–437)

No implementation code changed — these are pure test additions exercising existing matching logic.

**Why these tests specifically:**

Each test targets a specific property of price-time priority that could be broken by a subtle implementation error:

1. **`FIFOPartialFillThirdOrderAtSamePrice`** — Three sells at the same price (qty 10 each), incoming buy for 25. The matching loop must:
   - Fill the first order completely (10)
   - Fill the second order completely (10)
   - Partially fill the third order (5 of its 10), leaving 5 resting

   This is the "2.5 orders" scenario. If the intrusive list traversal is wrong (e.g., accidentally starting from the tail instead of the head, or advancing the pointer incorrectly after `remove_order`), this test catches it. The partial fill of the third order is particularly interesting because it verifies that `resting->quantity -= fill_qty` works correctly mid-level without disrupting the list structure.

2. **`TradeSequenceStrictlyIncreasingWithinSubmission`** — Verifies that `next_trade_sequence_++` produces monotonically increasing values across all fills in a single `submit()` call. This seems trivial but would catch bugs like:
   - Accidentally resetting the counter between fills
   - Post-increment vs. pre-increment confusion (`trade_sequence` should be the *current* value, then increment for next)
   - Counter not being shared across levels (e.g., each level getting its own counter)

3. **`EventSinkOnTradeOrderMatchesEngineResponse`** — The engine has two output channels: `EngineResponse.trades` (returned to caller) and `EventSink::on_trade` (broadcast). Both must reflect the *same* trades in the *same* order. This test uses `RecordingEventSink` to capture `on_trade` calls, then compares field-by-field with `EngineResponse.trades`. If the implementation accidentally pushed to the vector before emitting to the sink (or vice versa), or if it emitted a different `Trade` object (e.g., a copy with stale fields), this test catches it.

4. **`PriceTimePrioritySweepsLevelsInOrder`** — Inserts sells at prices 102, 100, 101 (deliberately *not* in sorted order), then verifies an incoming buy at 102 fills at 100 first, then 101, then 102. This proves the `std::map<Price, PriceLevel, std::less<Price>>` (asks sorted ascending) is working correctly — the order book sorts by price regardless of insertion order, and `best_ask()` returns the lowest price via `asks_.begin()`.

5. **`FIFOWithinLevelRespectedForMultipleOrders`** — Five bids at the same price, incoming sell that partially fills the queue (consuming 2 full orders and partially filling the third). This is a deeper version of Test 1: more orders on the same level, verifying that the intrusive list's FIFO property holds for an arbitrary queue depth, not just 3 orders.

6. **`SelfCrossingMatchesNormally`** — Verifies R14 (no self-trade prevention). A buy that crosses a sell from the "same user" (in Phase 1 there's no user concept) should match normally. This confirms the engine has no accidental self-trade logic that would prevent fills.

**Why this architecture:**

These tests live in the same file as Task 9's matching engine tests (`matching_engine_test.cpp`) because they use the same fixture (`MatchingEngineTest` with a `RecordingEventSink`). They're logically grouped after the basic matching tests and before market-order tests (Task 11) and cancel tests (Task 12). This follows the GoogleTest convention of organizing tests by feature, with increasingly complex scenarios building on simpler ones.

The `RecordingEventSink` pattern is key to testing R20 (synchronous event emission). By capturing events in a vector during the matching call, we can assert:
- The number of events matches the number of trades
- The order of events matches the order of trades in `EngineResponse`
- The content of each event matches the corresponding trade

This would be impossible to test if events were asynchronous (Phase 4's threading concern) — but in Phase 1's single-threaded model, synchronous emission means the `RecordingEventSink` captures events deterministically.

**Why this data structure / algorithm, specifically:**

The tests exercise the interaction between:
- **`std::map` with comparators** — asks use `std::less<Price>` so `begin()` points to the lowest (best) ask. Bids use `std::greater<Price>` so `begin()` points to the highest (best) bid. The `PriceTimePrioritySweepsLevelsInOrder` test verifies this sorting is correct regardless of insertion order.
- **Intrusive doubly-linked list** — FIFO ordering within a level is maintained by `PriceLevel::push_back` (appends to tail). The matching loop calls `level->front()` repeatedly to get the oldest order. The `FIFOPartialFillThirdOrderAtSamePrice` and `FIFOWithinLevelRespectedForMultipleOrders` tests verify this queue behavior under partial consumption.
- **`remove_order` during iteration** — When a resting order is fully consumed, the matching loop calls `book_.remove_order(resting)`. This must not invalidate the iteration state — the next call to `level->front()` must return the *next* order in the queue. This works because intrusive list removal only touches the removed node's `prev`/`next` pointers (linking its neighbors to each other), not the head/tail pointers of the level (unless the removed node *was* the head or tail).

**Complexity:**
- **Test execution:** Each test is O(k) where k is the number of orders involved (typically 3–6). No test exercises more than 5 resting orders, keeping test runtime constant.
- **No complexity changes to the engine** — these tests verify existing O(k) matching behavior, not new algorithms.

**Benefits:**
1. **Regression safety:** If a future Phase 3 optimization (e.g., replacing `std::map` with a flat array) breaks the sorting invariant, these tests catch it immediately.
2. **Cross-channel consistency:** Verifying `EventSink` output matches `EngineResponse` output prevents bugs where one channel is updated but the other is forgotten — a common issue in systems with multiple output paths.
3. **Non-trivial FIFO verification:** Testing partial fills within a level exercises the interaction between `remove_order` (which modifies the intrusive list) and continued iteration (which reads from it). This is the exact pattern where use-after-free or iterator-invalidation bugs lurk.
4. **Insertion-order independence:** The `PriceTimePrioritySweepsLevelsInOrder` test specifically inserts orders in non-price order (102, 100, 101) to prove the book sorts correctly. A test that inserts in order (100, 101, 102) might pass even with a buggy comparator that happens to maintain insertion order.

**Drawbacks / tradeoffs accepted:**
1. **Test redundancy with Task 9:** Some overlap exists — Task 9 already tests basic FIFO and multi-level crossing. Task 10 adds depth (more orders, partial fills, non-sorted insertion) but doesn't test fundamentally new code paths. This is intentional: critical invariants deserve multiple tests with different shapes of input.
2. **No negative tests for FIFO violation:** We don't (and can't easily) test "what if FIFO were broken?" without intentionally introducing a bug. The tests verify the *positive* property holds, relying on the assertion that a correct implementation passes all tests and an incorrect one fails at least one.

**Alternatives considered and rejected:**
1. **Property-based testing (random order generation):** Instead of hand-crafted scenarios, generate thousands of random order sequences and verify invariants (e.g., trades always at resting price, FIFO always respected). This is valuable but deferred — Phase 1 is about targeted, readable tests that trace directly to requirements. Property-based testing adds complexity (generators, shrinking) without adding requirement coverage in this phase.
2. **Separate test file for price-time priority:** Could have created `tests/price_time_priority_test.cpp` to isolate these tests. Rejected because they use the same fixture and conceptually extend Task 9's matching tests — splitting into a separate file would fragment related tests.

**How this connects to what came before:**
- Task 9 implemented the matching loop (`match_against_book`) and basic tests. Task 10 adds stress tests for the *ordering guarantees* that loop must maintain.
- Task 7 (`PriceLevel`) implemented the intrusive list that these tests indirectly verify. If `push_back` or `remove` had a bug that only manifested with 3+ orders, Task 10's tests would catch it even though Task 7's tests didn't.
- Task 8 (`OrderBook`) implemented the `std::map`-based price tree. The `PriceTimePrioritySweepsLevelsInOrder` test proves the tree's comparator correctly sorts ask prices ascending regardless of insertion order.

**Check your understanding:**
1. Why does the `PriceTimePrioritySweepsLevelsInOrder` test insert sells in the order 102, 100, 101 (not 100, 101, 102)? What specific bug would be missed if sells were inserted in sorted order?
2. In `FIFOPartialFillThirdOrderAtSamePrice`, after the third order is partially filled (5 of 10 consumed), what is the state of the intrusive linked list at that price level? How many orders remain, and what are their quantities?
3. The `EventSinkOnTradeOrderMatchesEngineResponse` test clears `sink.trades` before the buy submission. Why is this necessary? What events are in the sink before that point?
4. If `match_against_book` emitted `on_trade` *after* pushing to the trades vector (instead of alongside), would `EventSinkOnTradeOrderMatchesEngineResponse` still pass? What about if it emitted *before* pushing?

### Task 11 — Market-Order Submission (`submit_market`)

**What it does:**
Implements `MatchingEngine::submit_market`, which handles incoming market orders — orders that match immediately against all available opposite-side liquidity without any price constraint, and never rest on the book. This completes the order-type coverage: after Task 9 (limit without matching), Task 10 (limit with matching), and now Task 11 (market orders), the engine can process both order types defined in the `NewOrder` variant.

**Exact locations:**
- `engine/matching_engine.cpp:82–110` — the `submit_market` implementation (replaces the stub from Task 9)
- `tests/matching_engine_test.cpp:415–576` — 12 GoogleTest cases covering all acceptance criteria
- `engine/matching_engine.hpp:57` — the `submit_market` private method declaration (unchanged from the earlier stub; the signature was already correct)

**Why this data structure / algorithm, specifically:**

The implementation follows the exact same pattern as `submit_limit`, with two critical differences:

1. **`match_against_book` is called with `std::nullopt` for `limit_price`** instead of the order's price. The matching loop already handles this case — when `limit_price` is `std::nullopt`, the crossing condition check (`if (limit_price.has_value()) { ... }`) is skipped entirely, meaning the market order crosses *every* price level on the opposite side, regardless of how far away the price is. This is the "no price ceiling/floor" behavior specified by R9.

2. **The remainder is NOT rested on the book.** After `match_against_book` returns, `submit_limit` checks `if (remaining > Quantity{0})` and adds a resting order to the book. `submit_market` simply... doesn't. The remaining quantity is reported in `EngineResponse.remaining_qty` but no `Order` is created, no book insertion happens. This is the "cancel remaining" behavior per R10.

The full flow:
```
submit_market(order):
  1. Validate quantity > 0           → InvalidQuantity if not
  2. Check ever_seen_ids_            → DuplicateOrderId if found
  3. Insert into ever_seen_ids_      (lifetime-unique tracking)
  4. Emit on_order_accepted          (R16: before matching)
  5. match_against_book(side, id, remaining, std::nullopt)
  6. Return {Accepted, trades, remaining}  ← NO book insertion
```

Compare to `submit_limit`:
```
submit_limit(order):
  1. Validate quantity > 0           → InvalidQuantity if not
  2. Validate price > 0             → InvalidPrice if not
  3. Check ever_seen_ids_            → DuplicateOrderId if found
  4. Insert into ever_seen_ids_      (lifetime-unique tracking)
  5. Emit on_order_accepted          (R16: before matching)
  6. match_against_book(side, id, remaining, order.price)
  7. If remaining > 0: rest on book  ← DIFFERENT
  8. Return {Accepted, trades, remaining}
```

The differences (no price validation, nullopt limit, no resting) are all structural consequences of what a market order *is*: it has no price (enforced by the type system — `MarketOrder` has no `price` field), it crosses everything, and it never rests.

**Why this architecture / pattern:**

The market order implementation reuses `match_against_book` rather than having its own matching loop. This was a deliberate design decision made in `design.md` §5:

> "One matching algorithm with a parameter" — duplicating the loop for market orders would risk the two copies drifting.

The alternative (a separate `match_market_against_book` function) would start identical and gradually diverge as one gets bug fixes the other doesn't. By parameterizing on `std::optional<Price> limit_price`, we get both behaviors from one loop, and the loop's correctness (already proven by Task 10's tests for limit orders) carries over to market orders for free.

The type-level guarantee that `MarketOrder` has no price field (enforced by the `std::variant<LimitOrder, MarketOrder>` design from Task 4) means the engine *cannot* accidentally pass a price to `match_against_book` for a market order — there's no price to read. This turns a potential runtime bug ("forgot to pass nullopt") into a compile-time impossibility.

**Complexity:**
- **Time:** O(k) where k is the number of resting orders consumed — identical to limit-order matching. The only difference is that k can be larger (a market order sweeps all levels, while a limit order stops at its price), but the per-fill cost is the same
- **Space:** O(k) for the `vector<Trade>` of fills returned. Market orders have no resting state (no `Order` struct created, no book insertion), so they use *less* space than limit orders that partially rest

**Benefits:**
1. **Code reuse without abstraction overhead:** One matching loop serves both order types. No virtual dispatch, no strategy pattern, no inheritance hierarchy — just a `std::optional<Price>` parameter. The recruiter sees restraint: the simplest mechanism that works
2. **Type-safety prevents a class of bugs:** A `MarketOrder` physically cannot carry a price. You can't accidentally write `match_against_book(side, id, remaining, order.price)` in `submit_market` because `order.price` doesn't exist — the compiler would reject it
3. **Clear separation of "what matched" from "what rests":** The matching loop returns trades and a modified `remaining`. The *caller* decides whether to rest the remainder (limit: yes, market: no). This makes the no-rest behavior explicit — it's not buried inside the matching loop behind a flag

**Drawbacks / tradeoffs accepted:**
1. **A market order on an empty book still goes through the full validation and event-emission path before discovering there's nothing to match.** This is wasted work (emit `on_order_accepted`, enter `match_against_book`, immediately exit because `best_ask() == nullptr`). Alternative: check "is opposite side empty?" before accepting. Rejected because: the engine's acceptance policy should be consistent ("we accepted your order, here's what happened" — even if "what happened" is "nothing matched"). This matches real exchange behavior: a market order submitted to an empty book is acknowledged, then cancelled by the exchange's logic
2. **`EngineResponse.remaining_qty` for a market order that didn't fully fill reports the unfilled amount, which the caller must interpret as "discarded," not "resting."** There's no flag in `EngineResponse` distinguishing "remaining because it's resting" (limit) from "remaining because it was discarded" (market). The caller must know what type of order they submitted to interpret the response correctly. Alternative: add an `OrderStatus` field (`Resting`, `FullyFilled`, `PartiallyFilledAndCancelled`). Deferred because Phase 1's CLI knows what it submitted and Phase 6's market data feed uses `EventSink`, not `EngineResponse`
3. **Market orders sweep all available liquidity regardless of price distance.** A market buy will fill against a sell at price 999,999,999 if that's the only order on the book. In production, this would be mitigated by price bands / circuit breakers (Phase 8's risk engine). In Phase 1, "no limits" is correct per R9

**Alternatives considered and rejected:**

1. **Separate `match_market_against_book` function:**
   - Would duplicate the entire matching loop with the only difference being "no price check"
   - Rejected per design.md §5: one loop, parameterized by `std::optional<Price>`, avoids drift between two near-identical implementations

2. **Market orders that rest with a "market price" sentinel (e.g., `Price{INT64_MAX}` for buys):**
   - Some exchanges implement market orders as limit orders with an extreme price
   - Rejected because: (a) the type system says `MarketOrder` has no price — adding a fake price violates the design, (b) a resting market order at `INT64_MAX` would be visible in the book, confusing `PRINT_BOOK` output, (c) R10 explicitly says "a Market order never rests on the book"

3. **`InvalidPrice` error for market orders (since they have no price):**
   - Structurally impossible: `MarketOrder` has no price field, so there's nothing to validate. The type system makes this a non-issue. R11 explicitly states this: "`InvalidPrice` therefore never applies to a `MarketOrder`"

4. **Partial fill + IOC-style kill (vs. simply reporting remainder):**
   - IOC (Immediate or Cancel) semantics would emit an `on_order_cancelled` event for the unfilled portion
   - Rejected for Phase 1: the current behavior is simpler (just report remaining in `EngineResponse`; no additional event for the "cancelled" portion). Phase 8's risk engine or a later order-type expansion might add IOC/FOK as explicit order types with distinct semantics

**How this connects to what came before:**
- Task 4 established the type-level separation (`LimitOrder` with price, `MarketOrder` without). Task 11 is where that design pays off: `submit_market` *cannot* accidentally reference a price because the type forbids it
- Task 10 built and tested `match_against_book`. Task 11 reuses it with one different argument (`std::nullopt`), proving the loop's generality
- Task 9's `submit_limit` set the validation pattern (check qty, check duplicates, insert ID, emit accepted). Task 11 follows the same pattern minus the price check

**Check your understanding:**
1. If `match_against_book` didn't use `std::optional<Price>` but instead always took a `Price` parameter, how would you implement "no price limit" for market orders? What's wrong with using `Price{INT64_MAX}` as "no limit" for buys?
2. Why does `submit_market` still insert the order ID into `ever_seen_ids_` even though the market order never rests? What would go wrong if it didn't?
3. A market sell for quantity 100 arrives. The book has buy orders at prices 50 (qty 30), 49 (qty 30), and 48 (qty 30). The market sell fills 90 and reports `remaining_qty = 10`. Is this 10 resting anywhere? How would a future market-data subscriber know about the unfilled portion?
4. The `on_order_accepted` event for a market order has `quantity = 20` (the original submitted quantity). After matching, `remaining_qty = 15` (only 5 filled). The event was emitted *before* matching. Could a subscriber of `EventSink` figure out how much actually filled just from the events, without seeing `EngineResponse`? (Hint: what events do they get?)


### Task 12 — Cancel Logic (`MatchingEngine::cancel`)

**What it does:**

Implements the cancel operation for the matching engine. When a client submits a `CANCEL` request with an `OrderId`, the engine looks up whether that order is currently resting in the book. If it is, the engine removes it from the book, emits an `on_order_cancelled` event via the injected `EventSink`, and returns `Accepted` with the remaining quantity the order had at the moment of cancellation. If the order is *not* currently resting (because it never existed, was already fully filled, or was already cancelled), the engine returns `UnknownOrderId` with no event emitted.

**Exact locations:**
- Implementation: `engine/matching_engine.cpp:24–41` (the `cancel` method body)
- Declaration: `engine/matching_engine.hpp:42` (`EngineResponse cancel(OrderId id) override;`)
- Tests: `tests/matching_engine_test.cpp` — the "Task 12" section (tests named `CancelRestingOrderSucceeds`, `CancelPartiallyFilledOrderReportsCorrectRemaining`, `CancelUnknownIdReturnsUnknownOrderId`, `CancelAlreadyFilledOrderReturnsUnknownOrderId`, `DoubleCancelReturnsUnknownOrderId`, `CancelDecrementsOrderCountAndRemovesFromLookup`, `CancelRejectionNoEvents`)

**Why this data structure / algorithm, specifically:**

Cancel is implemented as a two-step O(1) lookup + O(1) removal:

1. **Lookup:** `book_.find_order(id)` — this calls `orders_.find(id)` on the `unordered_map<OrderId, unique_ptr<Order>>`, which is O(1) amortized (hash table lookup).

2. **Removal:** `book_.remove_order(order)` — this does three things:
   - Unlinks the order from its intrusive doubly-linked list via `PriceLevel::remove(order)` using the embedded `prev`/`next` pointers: O(1), no traversal needed.
   - Checks if the price level is now empty and removes it from the `std::map` price tree if so: O(log P) in the worst case, but only when the last order at a level is cancelled — amortized over many cancels at the same level, each cancel is O(1) for the unlink, with the O(log P) tree-erase amortized across all orders that were at that level.
   - Erases the `unique_ptr<Order>` from `orders_`: O(1) amortized (hash table erase).

The critical design insight: cancel does **not** consult `ever_seen_ids_`. That set tracks lifetime-unique IDs (for duplicate-ID rejection on `submit`). Cancel only cares about "is this order *currently resting*?" — which is answered by `find_order` alone. An order that was accepted but has since been fully filled is in `ever_seen_ids_` but not in the book's `orders_` map, so `find_order` returns `nullptr` and cancel correctly returns `UnknownOrderId`.

This distinction is subtle and important: "has this ID ever been used?" (submit's concern, answered by `ever_seen_ids_`) vs. "is this order currently in the book?" (cancel's concern, answered by `find_order`) are two different questions answered by two different data structures.

**Why this architecture / pattern:**

The cancel implementation follows the same pattern as the rest of the engine's API:
- Validate first (is it in the book?), fail fast with a structured error result if not
- Mutate state (remove from book)
- Emit event (`on_order_cancelled`)
- Return structured response

The event is emitted *after* the mutation, which is a deliberate ordering choice: if the EventSink observer were to query the book during `on_order_cancelled`, it would see the order already gone. This matches the semantics: the event says "this order *was* cancelled" (past tense), not "this order *is being* cancelled." The same synchronous-before-response guarantee (R20) applies here as for submit/trade events.

**Complexity:**
- **Time:** O(1) amortized for the entire cancel operation (hash lookup + intrusive-list unlink + hash erase). The O(log P) tree-level removal only fires when the cancelled order was the *last* order at its price level — in the common case (multiple orders at the same price), it's pure O(1).
- **Space:** O(1) — cancel deallocates space (destroys the `Order` object, removes from hash map), it doesn't allocate anything.

**Benefits:**
1. **O(1) cancel is a hard requirement met with minimal machinery:** No auxiliary data structures beyond what already exists (the `orders_` map, the intrusive list, the `level` back-pointer). The `Order::level` back-pointer (8 bytes per order) is what makes this possible — without it, we'd need to look up the price level from the order's price in the std::map, turning O(1) into O(log P)
2. **Clean separation from lifetime-uniqueness logic:** Cancel doesn't touch `ever_seen_ids_`, and submit doesn't look at the book's `orders_` map for uniqueness. Each data structure has exactly one responsibility
3. **No special cases:** The same `remove_order` code path handles both "engine removes during matching" (R7: fully consumed resting order) and "client cancels" (R12). One removal mechanism, two callers — no risk of the two paths diverging
4. **Event emission is consistent with the rest of the engine:** `on_order_cancelled` follows the same pattern as `on_order_accepted` and `on_trade` — called synchronously, only on actual state changes (R18), never on rejections (R19)

**Drawbacks / tradeoffs accepted:**
1. **A cancelled order's ID remains in `ever_seen_ids_` forever.** This means the ID can never be reused, even after cancellation. This is correct per the lifetime-uniqueness rule (§2.1), but it means the `ever_seen_ids_` set grows monotonically. For Phase 1 (correctness-first, no production runtime concerns), this is explicitly accepted. In a production system, you'd eventually need to bound this (e.g., sliding window of recent IDs, or a counter-based scheme where IDs are server-assigned and inherently unique)
2. **Cancel of an already-filled order returns `UnknownOrderId`, which is the same error as "ID never existed."** The caller cannot distinguish "I cancelled something that filled" from "I typo'd the ID." Alternative: add a `AlreadyFilled` or `AlreadyGone` error variant. Rejected because: R13 explicitly groups both cases under `UnknownOrderId`, and real exchanges (FIX protocol's `OrderCancelReject` with `CxlRejReason=1` "Unknown order") do the same. Adding more granular errors would be a later enhancement, not a correctness issue
3. **No "cancel pending" state or partial-cancel.** Cancel is all-or-nothing: the entire remaining quantity is removed. There's no "cancel 10 of the 50 remaining." This matches Phase 1's scope (no cancel-replace / partial cancel order types). Phase 8+ could add `CancelReplace` as a new operation

**Alternatives considered and rejected:**

1. **Check `ever_seen_ids_` first, return a different error if ID was seen but isn't resting:**
   - Would allow callers to distinguish "unknown ID" from "known but already filled/cancelled"
   - Rejected because requirements.md R13 explicitly says both cases return `UnknownOrderId`. Adding a separate error code would over-engineer beyond the spec and would add a hash lookup on the rejection path for no correctness benefit

2. **Remove the order ID from `ever_seen_ids_` on cancel (allowing reuse):**
   - Would make IDs recyclable after cancellation
   - Rejected because §2.1 explicitly mandates lifetime-unique IDs. Removing from the set would violate the guarantee and break FIX-gateway semantics where `ClOrdID` is never reused within a session

3. **Emit `on_order_cancelled` before `remove_order` (event before mutation):**
   - Would allow an EventSink observer to query the book and still see the order during the event callback
   - Rejected because: the event's semantics are "this order has been cancelled" — if an observer sees it still in the book, that's confusing. Also, emit-after-mutation matches the principle of least surprise and is consistent with how `on_trade` works (the trade has already happened when the event fires)

**How this connects to what came before:**
- Task 8 built `OrderBook::find_order` and `OrderBook::remove_order` — cancel is simply the engine-level composition of these two primitives with validation and event emission on top
- Task 7's `PriceLevel::remove` does the actual intrusive-list unlink — that's the O(1) mechanical core that makes cancel fast
- Task 9 established the validation-then-mutate-then-emit pattern in `submit_limit`; cancel follows the same three-phase structure
- The `Order::level` back-pointer (Task 4 / design.md §1) is what enables the O(1) path from `Order*` → `PriceLevel` → unlink, without a separate price-tree lookup

**Check your understanding:**
1. Why does `cancel` use `book_.find_order(id)` rather than checking `ever_seen_ids_.contains(id)` first? What would go wrong if you checked `ever_seen_ids_` and returned a different error for "known but not resting"?
2. An order at price 100 (quantity 50) is partially filled down to quantity 30, then cancelled. What does `remaining_qty` in the `EngineResponse` show? What does the `OrderCancelled` event's `remaining_qty` show? Are they the same?
3. After a cancel, is the order's ID available for reuse in a new `submit` call? Why or why not?
4. Consider the sequence: submit order A, submit order B (which fully fills A via matching), then cancel A. What happens? Trace through the code path and explain which branch `cancel` takes.


### Task 13 — Integration Tests (Full Order Lifecycle)

**What it does:**

Adds a dedicated integration test suite (`tests/integration_test.cpp`) that exercises the *full* order lifecycle — multi-step scenarios where multiple operations compose together to verify the engine behaves correctly in realistic conditions, not just in isolation. While Tasks 9–12's unit tests verify individual operations work (submit, match, cancel), this task verifies that *sequences* of operations produce correct cumulative state. It's the difference between testing a single gear vs. testing that the whole gearbox meshes correctly.

**Exact locations:**
- `tests/integration_test.cpp` — 8 integration test cases in the `IntegrationTest` fixture
- `CMakeLists.txt:96–98` — the `integration_test` target definition, linking `engine`, `GTest::gtest_main`, and `miniexchange_warnings`, registered with `gtest_discover_tests`

**Why these specific scenarios:**

Each test exercises a *distinct lifecycle pattern* that earlier unit tests cannot easily cover:

1. **`SweepMultiLevelBookWithLargeCrossingOrder`** — Builds a 7-order book (5 sells + 2 buys at non-crossing prices), then sweeps 4 ask levels with a single aggressive buy. Verifies: (a) trades are generated in price-priority order, (b) trade sequences are monotonically increasing, (c) the partially-filled sell has the correct remaining quantity, (d) orders that weren't crossed are still intact, (e) EventSink received exactly the right number of events. This tests the full "build → sweep" workflow that a real trading session produces constantly.

2. **`PartialFillThenCancelRemainder`** — Submit → partial fill → confirm resting state → cancel the remainder. The critical assertion: `on_order_cancelled` reports `remaining_qty = 30` (the quantity *after* partial fill), not the original 50. This catches a bug where cancel might report the original submitted quantity instead of the current resting quantity — a subtle issue because `OrderAccepted` carries the *original* quantity while `OrderCancelled` must carry the *current* quantity.

3. **`InterleavedLimitAndMarketOrders`** — Alternates limit and market orders in a single scenario: place limits, then partially consume with a market order, then sweep more with another market order, then cross the remaining bids with a limit sell. This proves the two order types coexist correctly — the market order's "no resting" behavior doesn't interfere with subsequent limit operations, and the book state is consistent after each step.

4. **`FillBookCancelEverythingRefillAndSweep`** — The most aggressive lifecycle test: build a book, cancel every order, verify the book is truly empty, confirm cancelled IDs can't be reused (lifetime-unique per §2.1), refill with fresh IDs, sweep the refilled book. This exercises the entire lifecycle: create → destroy → attempt reuse (rejection) → recreate → consume. It specifically catches bugs where cancel leaves ghost state (e.g., empty price levels left in the tree, stale entries in the orders map, `order_count_` out of sync).

5. **`SelfCrossingMultipleOrdersMatchNormally`** — Per R14 (no self-trade prevention in Phase 1), the "same client" submits both sells and buys that cross. This is the integration-level version of the unit test in Task 10: here it happens across multiple price levels on both sides, verifying that self-crossing works identically to normal matching even in a complex multi-level scenario.

6. **`BookAccessorConsistencyThroughLifecycle`** — A step-by-step lifecycle that asserts `order_count()` and `find_order()` correctness after every single operation: add 3 buys → add 3 sells → cancel best bid → cancel best ask → sweep remaining asks → add new ask → market sell sweeps bids. The focus is on the *read-only accessor* (`engine.book()`) being consistent at every point — not just at the end, but after each intermediate mutation.

7. **`SamePriceLevelFIFOWithCancelsAndFills`** — 5 sell orders at the same price, then cancel 2 from the middle, then sweep the remaining 3. Verifies that FIFO ordering is preserved after cancels: cancelling orders 70 and 72 leaves the queue as [71, 73, 74], and matching consumes them in that order. This is critical because intrusive-list removals from the middle must correctly relink neighbors — a bug here would produce wrong fill order.

8. **`LargeScaleMarketSweepAfterPartialConsumption`** — 6 orders across 3 bid levels (2 per level), partially consume the top level with a limit sell, then sweep 4 of the remaining 5 orders with a market sell. The large quantities (100 per order) and multi-step consumption (limit partial → market sweep) exercise the matching loop's interaction with partially-filled orders and its level-traversal logic at scale.

**Why this data structure / algorithm, specifically:**

The tests themselves don't introduce new algorithms — they compose existing engine operations. However, they test the *interaction* between data structures that unit tests cover in isolation:

- **Intrusive list** (FIFO) + **cancel** (arbitrary removal) — Scenario 7 specifically tests that FIFO is maintained after non-front removals
- **Price tree** (level ordering) + **matching** (level consumption) + **cancel** (level removal) — Scenarios 1, 4, and 6 verify that the tree's begin() (best bid/ask) updates correctly as levels are created, consumed, and removed by different operations
- **`ever_seen_ids_`** (lifetime-uniqueness) + **cancel** (order removal) — Scenario 4 verifies that cancelling an order doesn't make its ID reusable
- **`order_count_`** (bookkeeping) + all mutations — Scenario 6 asserts `order_count()` at every step, catching off-by-one errors in the counter maintenance

**Why this architecture / pattern:**

The integration tests live in a separate file (`integration_test.cpp`) rather than in `matching_engine_test.cpp` because they serve a different purpose:

- **Unit tests** (Tasks 9–12): test one requirement at a time, minimal setup, verify one property per test. Example: "duplicate ID is rejected" uses exactly 2 orders.
- **Integration tests** (Task 13): test realistic multi-step workflows, substantial setup (5–10 orders), verify cumulative state correctness. Example: "build a deep book, sweep it, verify everything" uses 8 orders.

A separate file keeps the test organization clean: a developer looking for "how is R12 tested?" goes to `matching_engine_test.cpp`. A developer looking for "does the full system hold together?" goes to `integration_test.cpp`.

The `RecordingEventSink` is reused from the same pattern established in Task 9's tests, with an added `clear()` method for convenience — tests that have multiple phases can clear the recorded events between phases to focus assertions on just the latest batch.

**Complexity:**
- Each test is O(k) where k is the number of orders involved (typically 5–10). Total execution time for all 8 tests: < 1ms (as measured by the test run).
- No new engine code is introduced — the tests exercise existing O(1)/O(log P)/O(k) operations in composition.

**Benefits:**
1. **Catches composition bugs that unit tests miss:** A bug where `remove_order` slightly corrupts the intrusive list on partial-level removal would pass all unit tests (which test single-level scenarios) but fail integration Scenario 7 (cancel from middle of a multi-order level, then sweep the rest).
2. **Regression safety for refactoring:** When Phase 3 replaces `unique_ptr<Order>` allocation with a memory pool, these integration tests verify the full lifecycle still works end-to-end — they don't depend on allocation details, only on behavioral correctness.
3. **Documents realistic usage patterns:** A recruiter reading these tests sees what "using a matching engine" looks like in practice — not just isolated operations, but realistic trading sessions with multiple participants, partial fills, and lifecycle events.
4. **Confidence in `order_count()` consistency:** Scenario 6 specifically tracks order count after every mutation, catching bookkeeping bugs that accumulate over sequences of operations but aren't visible in single-operation tests.

**Drawbacks / tradeoffs accepted:**
1. **Tests are longer and more complex than unit tests.** A failing integration test requires more debugging effort — the failure could be in any of 5+ operations within the scenario. This is inherent to integration testing: you trade diagnostic precision for coverage breadth.
2. **No `best_bid()`/`best_ask()` assertions through the const `engine.book()` accessor.** The `OrderBook::best_bid()` and `best_ask()` methods are non-const (they return `PriceLevel*`, not `const PriceLevel*`), but `EngineAPI::book()` returns `const OrderBook&`. The integration tests work around this by using `find_order()` and checking `order->price` to infer top-of-book state. This is a known const-correctness gap in the `OrderBook` API — adding const overloads would be the proper fix (returning `const PriceLevel*`), but it's outside this task's scope.
3. **Some overlap with unit tests.** The sweep-and-verify pattern in Scenario 1 covers ground similar to Task 10's `PriceTimePrioritySweepsLevelsInOrder`. The difference is scale (7 orders vs. 3) and scope (full lifecycle state verification vs. just trade correctness). The redundancy is acceptable for a critical code path.

**Alternatives considered and rejected:**

1. **Property-based testing (generate random order sequences, verify invariants):**
   - Could generate thousands of random scenarios and assert invariants (e.g., `order_count` always matches actual resting orders, trades always at resting price)
   - Rejected for Phase 1: hand-crafted scenarios with explicit expected values are easier to debug and trace to requirements. Property-based testing is valuable but adds generator complexity without covering specific lifecycle patterns
   
2. **Single monolithic "full lifecycle" test:**
   - One giant test that does everything: builds, sweeps, cancels, rebuilds, re-sweeps
   - Rejected because: a failure in step 47 is nearly impossible to diagnose. Separate focused scenarios (each testing one lifecycle pattern) provide better failure isolation while still being "multi-step"

3. **Testing via the CLI app (end-to-end through `apps/cli/`):**
   - Would test the full stack: parsing → engine → formatting
   - Rejected for this task because: CLI testing is Task 14's responsibility. Integration tests should exercise the *engine* API directly, without CLI parsing as a confounding factor. If a test fails, you want to know it's an engine bug, not a parser bug

**How this connects to what came before:**
- Tasks 9–12 built the engine's four operations (submit_limit, match, submit_market, cancel) and unit-tested each in isolation. Task 13 verifies these operations *compose* correctly
- Task 8's `OrderBook` provides the `find_order()` and `order_count()` methods used extensively for state assertions
- Task 7's `PriceLevel` (intrusive list) is indirectly tested by Scenario 7's "cancel from middle, then sweep" pattern — if the list unlinking is wrong, FIFO would be broken
- The `RecordingEventSink` pattern from Task 9 is reused with a `clear()` helper for multi-phase scenarios

**Check your understanding:**
1. Why is Scenario 4 ("cancel everything, try to reuse IDs, refill") important? What specific bug would it catch that Scenario 2 ("partial fill then cancel") wouldn't?
2. In Scenario 7, after cancelling orders 70 and 72 from the middle of the queue [70, 71, 72, 73, 74], what must the intrusive list's `prev`/`next` pointers look like for the remaining orders [71, 73, 74]? Draw the linked list state.
3. Why do the integration tests avoid calling `engine.book().best_bid()` / `best_ask()` and instead use `find_order()` + check `order->price`? What would need to change in the `OrderBook` interface to make direct best-of-book assertions possible through the const reference?
4. Scenario 8 partially fills the top level with a limit sell, then sweeps remaining with a market sell. Why test this in two steps rather than one large market sell? What different code path does the limit sell exercise compared to the market sell?


### Task 14 — CLI App (`apps/cli/`)

**What it does:**
Implements the CLI composition root — the first executable that wires together a parser, the matching engine, and an output formatter into a usable program. This is where the Ports & Adapters architecture becomes tangible: you can run `mini_exchange`, type commands at stdin, and watch the engine process orders and print results in real time.

The CLI app consists of three components:
1. **`CLIParser`** — turns raw text lines from stdin into typed commands (`LimitOrder`, `MarketOrder`, `CancelRequest`, `PrintBookRequest`, `QuitRequest`, or `ParseError`)
2. **`ConsolePrinter`** — formats `EngineResponse` and book state as human-readable text on stdout
3. **`main.cpp`** — the composition root that constructs the engine, wires it to the parser and printer, and runs the read-eval-print loop

**Exact locations:**
- `apps/cli/cli_parser.hpp` — `CLIParser` class declaration and `ParseResult` variant type
- `apps/cli/cli_parser.cpp` — parsing logic (tokenization, command dispatch, type conversions)
- `apps/cli/console_printer.hpp` — `ConsolePrinter` class declaration
- `apps/cli/console_printer.cpp` — formatting logic (response rendering, book depth display)
- `apps/cli/main.cpp` — composition root (the `int main()` that ties everything together)
- `orderbook/order_book.hpp` (modified) — added `const BidTree& bids()` and `const AskTree& asks()` accessors for R15 PRINT_BOOK support
- `CMakeLists.txt` (modified) — added `mini_exchange` executable target

**Why this data structure / algorithm, specifically:**

The central design choice is `ParseResult = std::variant<LimitOrder, MarketOrder, CancelRequest, PrintBookRequest, QuitRequest, ParseError>`. This mirrors the same variant pattern used for `NewOrder` in the engine: the set of possible commands is closed (we know all command types at compile time), and each command carries different fields. The alternative — a single `Command` struct with an `enum CommandType` and optional/union fields — would lose type safety (e.g., a CANCEL command wouldn't structurally prevent having a `price` field that makes no sense).

For parsing itself: `std::istringstream` with `operator>>` for tokenization is deliberately simple. Competitive programmers will recognize this as "the way you read space-separated input in contests." It's not the fastest parser (string copies, heap allocations in `istringstream`) — but it doesn't need to be. The CLI is a human-facing tool, not a hot path. A real gateway (Phase 5's TCP adapter, Phase 9's FIX parser) would use zero-copy parsing with `std::string_view` and no heap allocation; the CLI can afford simplicity.

For number parsing: `std::from_chars` (C++17) is used instead of `std::stoul`/`std::stoll`. Why? `std::from_chars`:
- Never throws (returns an error code instead — matches the "no exceptions for expected outcomes" philosophy)
- Is locale-independent (no risk of commas being interpreted as decimal separators on non-English systems)
- Can detect "partially consumed input" (e.g., "123abc" fails because `result.ptr` doesn't point to end-of-string)
- Is faster than `stoul`/`atoi` (no locale lookup, no error string construction)

**Why this architecture / pattern:**

The CLI demonstrates Ports & Adapters in practice:

```
                    ┌──────────────────┐
    stdin ──→ CLIParser ──→ │   EngineAPI*     │ ──→ MatchingEngine
                            │   (the port)     │
    stdout ←─ ConsolePrinter ←─ │                  │
                    └──────────────────┘
```

`main.cpp` accesses the engine *exclusively* through `EngineAPI*`, never through `MatchingEngine` directly:
```cpp
miniexchange::MatchingEngine engine;
miniexchange::EngineAPI* api = &engine;  // only this pointer is used from here on
```

This means:
- If you replace `MatchingEngine` with a `MockEngine` for testing, zero changes to parsing/printing code
- Phase 3 (memory pool) can swap the engine implementation behind `EngineAPI*` without touching `apps/cli/`
- The CLI proves the port abstraction works — it's not just a theoretical pattern, it's exercised by real code

**Per requirements.md §7:** the CLI does NOT implement `EventSink`. With a single-user app and synchronous execution, `EngineResponse` alone gives the caller everything it needs (status, trades, remaining qty). `EventSink` exists as a port for Phase 2's benchmark observer and Phase 6's UDP feed — it would be over-engineering to implement it here when nobody benefits from the broadcast. The default `NullEventSink` singleton handles the engine's `sink_->on_trade(...)` calls as no-ops.

**The `ParseResult` variant approach vs. alternatives:**

Why not `std::optional<NewOrder>` with a separate error channel?
- CANCEL, PRINT_BOOK, and QUIT aren't orders. They're distinct command types with different handling. A variant captures "this is one of N possible outcomes" cleanly, while `optional<NewOrder>` would need out-of-band signaling for non-order commands.

Why not an inheritance hierarchy (`class Command { virtual void execute(EngineAPI&) = 0; }`)?
- That puts execution logic inside the command object, coupling parsing to execution. With the variant + `std::visit`, parsing is pure (returns data), and execution is in `main.cpp`'s loop. Easier to test parsing independently.

**PRINT_BOOK and the `bids()`/`asks()` accessors:**

Requirements R15 says the engine must "expose a read-only view of the book... sufficient for a PRINT_BOOK command to render top-of-book and full depth." The existing `OrderBook` only exposed `best_bid()`/`best_ask()` (single-level access). For full depth display, the CLI needs to iterate all price levels.

Solution: added `const BidTree& bids() const` and `const AskTree& asks() const` to `OrderBook`. These return const references to the underlying `std::map`s, allowing read-only iteration without exposing mutating operations. The CLI iterates:
- Asks in reverse order (highest price at top, best ask closest to the spread line) for visual clarity
- Bids in forward order (which is already highest-first due to `std::greater<Price>` comparator)

This keeps all I/O in `apps/cli/` while giving it enough access to render a complete depth display.

**Complexity:**
- **Parsing:** O(n) where n is line length — tokenization and `from_chars` are linear in the input
- **Printing:** O(L) where L is number of price levels in the book (only for PRINT_BOOK; trade/response printing is O(k) where k is number of trades, typically small)
- **Space:** No persistent state in CLIParser or ConsolePrinter — they're stateless services. The only state is in the engine itself

**Benefits:**
1. **Separation of concerns:** Parsing, engine logic, and formatting are three separate translation units with no coupling between them beyond the shared types in `core/`
2. **Testability:** `CLIParser` can be tested independently (give it a string, check the variant). `ConsolePrinter` can be tested by passing it a `std::ostringstream` instead of `std::cout`
3. **Composition root pattern:** `main.cpp` is trivially short (~50 lines of wiring) — all logic lives in the components. This makes it easy to write different `main.cpp` files for different scenarios (the Phase 2 benchmark app will have its own `main.cpp` that wires the engine to a benchmark harness instead of a CLI)
4. **Graceful error handling:** Malformed input produces a clear error message, not a crash. The parser never throws for bad input — it returns a `ParseError` variant that the loop handles normally

**Drawbacks / tradeoffs accepted:**
1. **`total_quantity()` staleness on partial fills:** The PRINT_BOOK display uses `PriceLevel::total_quantity()`, which is maintained on push/remove but NOT updated when the matching loop partially decrements a resting order's quantity. This means after a partial fill, the displayed aggregate quantity at a level may be higher than the actual sum of resting order quantities. This is a pre-existing bug in the matching engine (the PriceLevel's total isn't kept in sync with direct `resting->quantity -= fill_qty` modifications), not something introduced by the CLI. It's flagged here as a known issue — the fix belongs in the engine/orderbook layer, not the CLI
2. **No color/formatting:** Output is plain text. A production CLI might use ANSI colors (green for fills, red for rejections) — deferred as purely cosmetic
3. **String-based tokenization allocates:** Each `parse()` call creates a `std::vector<std::string>` of tokens (heap allocation per token). For a human-driven CLI, this is irrelevant. For the benchmark harness (Phase 2), we'd bypass the CLI entirely and call `EngineAPI` directly
4. **QUIT only works via explicit command:** If the user sends EOF (Ctrl+D on Linux), the `while(getline)` loop exits naturally. If they type QUIT, the loop breaks. Both work, but there's no "are you sure?" prompt — acceptable for a developer tool

**Alternatives considered and rejected:**

1. **Implement `EventSink` in `ConsolePrinter`:**
   - Would allow printing events as they occur (e.g., "TRADE: buy_id=1 sell_id=2 price=10020 qty=20")
   - Rejected per requirements.md §7: "The CLI does not need to implement EventSink in Phase 1." With synchronous execution and a single user, `EngineResponse` already contains all the information the CLI needs. Adding EventSink would print duplicate information (once from the response, once from the event)

2. **Put CLI parsing in `adapters/cli/`:**
   - Rejected per structure.md: "apps/cli/ does not need a corresponding adapters/cli/ — its CLIParser/ConsolePrinter live directly inside apps/cli/ since nothing else reuses them." Extracting to `adapters/` is the right move *if* a second executable ever needs the same parser — not before

3. **Use `getopt` or a CLI framework (CLI11, argparse):**
   - These handle command-line *arguments* (flags, options), not interactive *commands* (line-by-line input). They solve a different problem. The CLI here is an interactive REPL, not a one-shot program with flags

4. **Use `std::stoul`/`std::stoll` for number parsing:**
   - Throws `std::out_of_range` / `std::invalid_argument` on bad input. We'd need try/catch, which conflicts with the "no exceptions for expected outcomes" philosophy. `std::from_chars` returns error codes instead, matching the engine's result-based error model

5. **Return `std::expected<Command, ParseError>` (C++23):**
   - More modern error handling than the variant approach. Rejected because: we target C++20 (locked in tech.md), and `std::expected` only conveys two states (success/failure) while we have six distinct outcomes. A variant is the right fit for "one of N possible types"

**How this connects to what came before:**
- Tasks 2–5 created the core types (`NewOrder`, `EngineResponse`, `Trade`) that the CLI parses into and formats from
- Task 6 defined `EngineAPI` and `EventSink` — the CLI depends on `EngineAPI*` as its only connection to the engine, validating the port abstraction
- Tasks 9–12 implemented the engine operations (submit, match, cancel) that the CLI exercises
- Task 13's integration tests verified engine correctness through direct API calls; the CLI verifies the same operations are accessible through text commands

**Check your understanding:**
1. Why does `main.cpp` use `EngineAPI* api = &engine;` and then only call methods through `api`, rather than calling `engine.submit(...)` directly? What would change if a Phase 3 pooled engine had a different concrete class name?
2. The `CLIParser` is stateless (no member variables). Why is this desirable? What would go wrong if the parser maintained state between calls (e.g., a "last command" buffer)?
3. Why does requirements.md §7 explicitly say the CLI does NOT implement `EventSink`? Under what conditions (future phases) would it make sense for a CLI to implement EventSink, and what would the output look like?
4. The PRINT_BOOK output shows asks in reverse order (highest price at top). Why is this the conventional way to display an order book, and how does it relate to the visual concept of "spread" between best bid and best ask?


### Task 15 — Edge-Case/Stress Tests + `PriceLevel::total_quantity()` Bug Fix

**What it does:**
Two things in one task:

1. **Fixes a latent bug** where `PriceLevel::total_quantity()` became stale after partial fills in the matching loop. The matching loop decremented `resting->quantity` directly without notifying the `PriceLevel` that its aggregate `total_qty_` had changed. This meant any code querying `total_quantity()` (e.g., `PRINT_BOOK` depth display, or Phase 6's market-data feed publishing per-level aggregate depth) would see an incorrect value after a partial fill.

2. **Adds a comprehensive edge-case and stress test suite** covering boundary conditions and high-volume scenarios that weren't covered by Tasks 9–12's targeted requirement-driven tests.

**Exact locations:**
- Bug fix:
  - `orderbook/price_level.hpp:37–41` — new `reduce_quantity(Quantity qty)` public method declaration
  - `orderbook/price_level.cpp:35–37` — implementation (`total_qty_ -= qty;`)
  - `engine/matching_engine.cpp:138–141` — call site in the matching loop, after `resting->quantity -= fill_qty`
- Tests:
  - `tests/edge_case_test.cpp` — new file with 15 GoogleTest cases
  - `CMakeLists.txt:91–93` — build target registration

**Why this data structure / algorithm, specifically:**

The root cause of the bug is the interplay between two responsibilities:

1. `PriceLevel::total_qty_` is maintained *incrementally* — on `push_back(order)`, it adds `order->quantity`; on `remove(order)`, it subtracts `order->quantity`. This makes `total_quantity()` O(1) to read without traversing the queue.

2. The matching loop modifies `resting->quantity` *in place* (decrementing by `fill_qty` on each fill). This is efficient — no reallocation, no re-insertion — but it means `resting->quantity` at the time of `remove()` is **zero**, not the original quantity. So `PriceLevel::remove()` subtracts 0, effectively making the removal a no-op for the aggregate.

The fix introduces `PriceLevel::reduce_quantity(Quantity qty)` — a minimal operation that decrements `total_qty_` without touching the intrusive list. The matching loop calls this for **every** fill (both partial and full), immediately after decrementing `resting->quantity`. For fully-consumed orders, the subsequent `remove()` call subtracts the now-zeroed `order->quantity` (i.e., subtracts 0), which is harmless. For partial fills, there is no `remove()` call, so `reduce_quantity` is the only thing keeping `total_qty_` in sync.

Why not fix `PriceLevel::remove()` to accept the "original quantity" as a parameter? Because `remove()` is also called by `cancel()`, where `order->quantity` is still the correct remaining value (cancel doesn't zero the quantity first). Changing `remove()`'s semantics would complicate the cancel path. The cleanest fix is the one that makes the matching loop responsible for keeping the aggregate in sync with its own in-place mutations — the matching loop is the code that *knows* how much it just subtracted.

**Why the tests were designed this way:**

Each edge-case test targets a specific boundary or stress condition:

| Test | What it probes |
|------|---------------|
| `ZeroQuantityLimitOrderRejected` | Re-confirms R3 explicitly as part of the edge-case suite |
| `SingleTickOrdersMatchCorrectly` | Minimum valid price (1) and quantity (1) — catches off-by-one in crossing condition |
| `ThousandOrdersAtSamePriceLevel` | 1000 orders on a single intrusive list — verifies the linked-list doesn't degrade or corrupt at scale. Also verifies FIFO across all 1000 fills |
| `CancelOnEmptyBookReturnsUnknownOrderId` | Empty-book cancel — ensures no nullptr dereference on an empty `orders_` map |
| `MarketOrderExactlyExhaustsLiquidity` | `remaining = 0` after matching all available liquidity exactly — boundary between "some left" and "exactly zero" |
| `RapidAddCancelSequencesNoCorruption` | 200 rapid add/cancel cycles — stress-tests that the intrusive list, hash map, and price tree stay in sync through repeated insertion/removal. Would catch use-after-free or dangling-pointer bugs |
| `RapidAddCancelWithMultipleOrdersOnSameLevel` | Interleaved cancels and adds at the same price level — verifies FIFO is maintained when the queue is modified between insertions |
| `ExactPriceEqualsCrosses_*` | Incoming order at exactly the best opposite price — the `>=` (buy) and `<=` (sell) crossing condition must trigger on equality, not just strict inequality |
| `TotalQuantityCorrectAfterPartialFill` | Multi-order level, partial fill of one order — the core bug-fix test |
| `TotalQuantityCorrectAfterMultiplePartialFills` | Repeated partial fills of the same order — `total_qty_` decrements correctly on each fill |
| `TotalQuantityZeroAfterFullConsumption` | All orders consumed → level removed from tree |
| `BidSideTotalQuantityCorrectAfterPartialFill` | Same bug verification on the bid side (not just asks) |

**Why this architecture / pattern:**

The tests access `total_quantity()` through the const `bids()`/`asks()` accessors on `OrderBook` (which return `const BidTree&` / `const AskTree&`). This is because `engine.book()` returns `const OrderBook&` — the test can't call non-const methods like `best_bid()` through a const reference. Using the const iterators is the correct way to observe book state from outside the engine without breaking encapsulation.

The `reduce_quantity` method lives on `PriceLevel` rather than being a free function or part of `OrderBook` because:
- `PriceLevel` owns `total_qty_` — it's the class responsible for the invariant "total_qty_ equals sum of order quantities in this queue"
- The matching loop already has a `PriceLevel*` in hand (the `level` variable in the inner while loop) — no additional lookup needed
- Keeping it as a method preserves encapsulation: external code can't arbitrarily modify `total_qty_`, only through the defined operations (`push_back`, `remove`, `reduce_quantity`)

**Complexity:**
- `reduce_quantity`: O(1) — a single integer subtraction
- The fix adds O(1) work per fill in the matching loop (one subtraction per fill, replacing the previous zero-cost-but-incorrect behavior)
- The 1000-order test is O(n) where n=1000, which runs in ~1ms on modern hardware

**Benefits:**
1. **Correct aggregate reporting:** `PRINT_BOOK` (and Phase 6's depth feed) will now show accurate per-level total quantities, even after partial fills. Without this fix, a level with 3 orders of qty 100 that all get partially filled to 50 would still show total_quantity = 300 instead of the correct 150
2. **Minimal invasiveness:** The fix adds one new method (4 lines total including header) and one line at the call site. No changes to `PriceLevel::remove()`, no changes to `cancel()`, no structural redesign
3. **Comprehensive edge-case coverage:** The test suite catches bugs that unit tests of individual components might miss — particularly interaction bugs between the matching loop and the data structures it operates on
4. **Stress-test for Phase 2:** The 1000-order test and 200-cycle rapid add/cancel test provide a baseline that Phase 2's benchmarks can build on. If Phase 3's memory-pool optimization introduces a bug, these tests catch it before benchmarks run on corrupted state

**Drawbacks / tradeoffs accepted:**
1. **`reduce_quantity` trusts the caller.** There's no assertion that `qty <= total_qty_`. If a caller passes a quantity larger than `total_qty_`, it would underflow (since `Quantity` is unsigned, this wraps to a huge number). In practice, the only caller is the matching loop, which guarantees `fill_qty <= resting->quantity <= total_qty_`. A debug-mode assertion here would be a good addition (deferred — Task 13's invariant assertions could cover this)
2. **The fix introduces a subtle ordering requirement:** `reduce_quantity(fill_qty)` must be called *before* `remove_order(resting)` for fully-consumed orders (because `remove_order` calls `remove()` which subtracts `order->quantity`, which is now 0). If someone reorders these lines, the aggregate would be doubly-decremented for partially-filled orders or not decremented at all for fully-filled ones. A code comment documents this dependency
3. **Test file duplication:** The `RecordingEventSink` class is duplicated in `edge_case_test.cpp`, `matching_engine_test.cpp`, and `integration_test.cpp`. A shared test utility would reduce this, but extracting it into a common header adds build-system complexity for marginal benefit at this stage

**Alternatives considered and rejected:**

1. **Modify `PriceLevel::remove()` to always subtract a passed-in quantity (not `order->quantity`):**
   ```cpp
   void remove(Order* order, Quantity qty_to_subtract);
   ```
   - Would work for the matching loop (pass `fill_qty` for fully consumed)
   - Breaks `cancel()` path, which doesn't know the "original" quantity — it just wants to subtract whatever's left
   - Rejected: changing `remove()` semantics affects all callers, and the cancel path currently works correctly

2. **Store the fill quantity decrement inside `Order` and have `remove()` read it:**
   - Adds a field to `Order` for bookkeeping state that only the matching loop cares about
   - Rejected: pollutes the data struct with engine-specific mechanics

3. **Don't decrement `resting->quantity` in-place; instead remove and re-insert with new quantity:**
   - Avoids the desync entirely (remove subtracts old quantity, push_back adds new quantity)
   - Catastrophically expensive: O(log P) tree operations + hash map operations per partial fill
   - Rejected per the "no allocation/unnecessary work in the hot path" principle

4. **Make `total_quantity()` computed (traverse the list on every call) instead of incremental:**
   - Would be "always correct" by definition (no aggregate to desync)
   - O(n) per call, where n is the number of orders at that level — unacceptable for Phase 6's market-data feed which publishes depth on every trade
   - Rejected: the incremental approach is O(1) to read; fixing the one place that desyncs it is far cheaper than making every reader pay O(n)

**How this connects to what came before:**
- Task 7 (`PriceLevel`) implemented the incremental `total_qty_` maintenance via `push_back`/`remove`. Task 15 reveals that this maintenance was incomplete for the matching loop's in-place quantity modification
- Task 9/10 (`match_against_book`) introduced the `resting->quantity -= fill_qty` line that creates the desync. Task 15 fixes the consequence
- Task 13 (invariant assertions) could add a debug-mode check that `total_qty_ == sum(order.quantity for order in queue)` after every matching operation — if it had been in place before Task 15, it would have caught this bug immediately

**Check your understanding:**
1. Why must `level->reduce_quantity(fill_qty)` be called for *all* fills (both partial and full), not just partial fills? What happens to `total_qty_` if it's only called for partial fills and a resting order is fully consumed?
2. The matching loop calls `reduce_quantity` *before* `remove_order` for fully-consumed orders. What would happen if the order were reversed (remove first, then reduce_quantity)? Would the level still exist to call `reduce_quantity` on?
3. The 1000-order test verifies FIFO by checking that trade `i` has `sell_order_id == OrderId{i+1}`. Why does this prove FIFO? What bug in `PriceLevel::push_back` would cause this test to fail?
4. Why can't the tests call `engine.book().best_ask()` directly? What does the const qualifier on `book()` prevent, and how do the tests work around it?


### Task 16 — GitHub Actions CI

**What it does:**
Defines a CI pipeline that automatically builds the project, runs all tests, and performs static analysis on every push to `main` and every pull request targeting `main`. This ensures the codebase stays green, and no one can merge broken code without noticing.

**Exact locations:**
- `.github/workflows/ci.yml` — the complete workflow definition (3 jobs: `build-and-test`, `clang-tidy`, `cppcheck`)

**Why this structure, specifically:**

The CI is split into three separate *jobs* (not three steps in one job):

1. **`build-and-test`** — the critical path. Checkout → install Ninja → configure → build → test. If this job fails, the PR is blocked. This is the gatekeeper: no merge without a green build and passing tests.

2. **`clang-tidy`** — static analysis. Runs with `continue-on-error: true`, meaning it reports warnings but never blocks a merge. Why non-blocking? Two reasons:
   - clang-tidy on Ubuntu 24.04 ships with whatever version is in the repos. New clang-tidy versions add new checks, and existing code that was clean under one version might suddenly trigger a new check — you don't want CI to go red overnight because of a tool update you didn't control.
   - Some clang-tidy checks produce false positives (especially in heavily templated code or with GoogleTest macros). Making it advisory rather than mandatory avoids "suppress this warning just to make CI green" noise.

3. **`cppcheck`** — a different static analyzer, also non-blocking (`continue-on-error: true`). cppcheck catches different classes of bugs than clang-tidy (it's better at flow-sensitive analysis but worse at type-system checks). Running both gives broader coverage without either being a hard gate.

**Why separate jobs instead of sequential steps:**
Separate jobs run in parallel on GitHub Actions. The `build-and-test` job finishes as fast as possible (typically ~2 minutes for this project), giving quick feedback on the PR. The static analysis jobs might take longer (clang-tidy in particular is slow on template-heavy code), but they don't block the developer from seeing whether their code compiles and tests pass.

If they were all steps in one job, a developer would wait for clang-tidy to finish (potentially 5+ minutes) before seeing whether their tests even pass.

**Why Ubuntu 24.04:**
Per `tech.md`'s locked stack: "Linux only, Ubuntu 24.04 LTS. No Windows/macOS support, ever." The CI runner matches the target platform exactly. Ubuntu 24.04 ships with GCC 13 (with GCC 14 available), which has complete C++20 support. No need for a compiler matrix — we test on the one platform we support.

**Why Ninja in CI:**
Ninja is the locked build generator (`tech.md`). It's not installed by default on GitHub runners (unlike Make), so we `apt-get install ninja-build`. Using the same generator in CI as locally avoids "works on my machine" build-system divergence.

**clang-tidy: why `find` + `xargs` instead of `run-clang-tidy`:**
`run-clang-tidy` (a wrapper script shipped with LLVM) runs clang-tidy on every file in the `compile_commands.json`. That includes FetchContent dependencies (GoogleTest, Google Benchmark) — we'd get thousands of warnings from third-party code we don't control.

Instead, we use `find` to explicitly enumerate only *our* source directories (`core/`, `orderbook/`, `engine/`, `interfaces/`, `apps/`, `tests/`), excluding everything in `build/` and `third_party/`. This keeps the output focused on code we own.

The `-p build` flag tells clang-tidy where to find `compile_commands.json` (generated by `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`), so it knows the correct include paths and compiler flags for each file.

**cppcheck: why those flags:**
- `--enable=all` — run all check categories (style, performance, portability, information, warning)
- `--suppress=missingIncludeSystem` — don't warn about system headers (like `<vector>`) that cppcheck can't find in its default search path
- `-I core/ -I orderbook/ ...` — tell cppcheck where to find our headers (it doesn't read `compile_commands.json` by default)
- `--error-exitcode=1` — return exit code 1 if any issue is found (but the `|| true` at the end means the step itself always "passes" — combined with `continue-on-error: true` on the job, this makes cppcheck purely informational)

**Why the build step in the clang-tidy job:**
clang-tidy needs the `compile_commands.json` (generated during configure) and sometimes needs generated headers (e.g., from FetchContent's config headers). The build step ensures all generated files exist before clang-tidy tries to analyze code that includes them. Without this, clang-tidy would error on `#include` lines that reference generated paths.

**Complexity:**
N/A — CI configuration, not an algorithm.

**Benefits:**
1. **Catches regressions immediately:** Every push and PR is automatically verified — no reliance on developers remembering to run tests locally
2. **Parallel feedback:** The main build/test result arrives fast; static analysis runs alongside without blocking
3. **Non-blocking analysis:** clang-tidy and cppcheck surface issues without creating false-positive build failures that erode trust in CI
4. **Matches production environment:** CI runs on the exact same OS and compiler as the target — no "it passed in CI but fails on my Linux box" surprises
5. **Minimal configuration:** The workflow is ~60 lines and uses only standard GitHub Actions (checkout) plus apt packages — no complex third-party actions that might break or be unmaintained

**Drawbacks / tradeoffs accepted:**
1. **FetchContent download on every run:** Each CI run downloads GoogleTest and Google Benchmark from GitHub. This adds ~10-20 seconds to the configure step. Alternative (caching the `build/_deps` directory with `actions/cache`) would speed this up, but adds complexity and can cause stale-cache bugs. For a project this size, 20 extra seconds is acceptable
2. **No benchmark run in CI:** `tech.md` says benchmarks are "optional/non-blocking." We don't run them here because benchmark results on shared CI runners are noisy (variable CPU allocation, noisy neighbors). Benchmarks are run locally with `taskset`/`numactl` for reproducible results. A future enhancement could run benchmarks and compare against a baseline, but that's Phase 2+ territory
3. **clang-tidy version is whatever Ubuntu 24.04 ships:** We don't pin a specific clang-tidy version. This means the checks might differ slightly from what a developer sees locally if they have a different version. For now this is acceptable — the job is advisory anyway
4. **No caching of build artifacts between jobs:** Each of the three jobs builds from scratch. Alternative (artifact upload/download between jobs) would save the re-build in clang-tidy/cppcheck jobs, but adds 30+ seconds of artifact upload/download that partially offsets the savings for a project this small

**Alternatives considered and rejected:**

1. **Single job with all steps sequential:**
   - Pros: Simpler YAML, no redundant builds
   - Cons: Developer waits for clang-tidy and cppcheck to finish before seeing test results. For a project that will grow, this latency compounds
   - Rejected: parallel jobs give faster feedback on the critical path

2. **Matrix build (GCC + Clang, multiple Ubuntu versions):**
   - Pros: Catches compiler-specific bugs, ensures portability across compiler versions
   - Cons: `tech.md` explicitly locks to "Linux only, Ubuntu 24.04" — there's no target to be portable to. A matrix would double CI time for no benefit
   - Rejected: we support exactly one platform

3. **Pre-built Docker container with all tools pre-installed:**
   - Pros: Faster CI (no apt-get install), reproducible environment, pinned tool versions
   - Cons: Extra maintenance burden (need to rebuild container when tool versions change), Docker Hub rate limits, overkill for 3 packages (ninja-build, clang-tidy, cppcheck)
   - Rejected: `apt-get install` of 3 packages takes ~5 seconds; not worth the Docker overhead

4. **GitHub Actions `cmake-build-action` or similar marketplace actions:**
   - Pros: Less YAML to write, handles some edge cases
   - Cons: Third-party actions can break, be abandoned, or have security issues. Our workflow is simple enough that raw `run:` commands are clearer and more maintainable
   - Rejected: standard commands are better than opaque abstractions for a project meant to demonstrate engineering competence

5. **Making clang-tidy and cppcheck blocking (fail the PR):**
   - Pros: Forces perfectly clean static-analysis results
   - Cons: False positives from either tool would block legitimate PRs. Tool version changes could break CI unexpectedly. Developers would add suppression comments "just to make CI green" rather than fixing real issues
   - Rejected: advisory analysis builds trust; blocking analysis erodes it unless you invest heavily in suppression management

**How this connects to what came before:**
- Task 1 established the CMake + Ninja build system that CI now automates
- Tasks 2–15 created all the source code and tests that CI builds and runs
- The `miniexchange_warnings` INTERFACE library (Task 1's CMakeLists.txt) applies `-Wall -Wextra -Wpedantic -Werror` only to our targets — this is what makes the build step catch real issues without failing on third-party code

**Check your understanding:**
1. Why does the clang-tidy job need to run `cmake --build build` before running clang-tidy, even though clang-tidy doesn't execute the code? What would fail without this step?
2. If you removed `continue-on-error: true` from the cppcheck job and cppcheck found a false positive, what would happen to a PR? Why is that worse than the current behavior?
3. The `find` command in the clang-tidy step explicitly lists directories (`core/ orderbook/ engine/ ...`) rather than using `find . -name '*.cpp'`. What would go wrong with the simpler command?
4. Why don't we run benchmarks in CI, even though `tech.md` mentions benchmarks? What makes CI runner results unreliable for performance measurement?


### Task 17 — README Polish + ADRs (Definition of Done Audit)

**What it does:**
Final Phase 1 documentation task: updates the README to reflect the completed project state, creates Architecture Decision Records (ADRs) for the two most fundamental design choices, and ensures the project is presentable to a recruiter who clones it cold.

**Exact locations:**
- `README.md` — rewritten to reflect Phase 1 complete state with build instructions, CLI usage example, architecture diagram, and documentation links
- `docs/adr/ADR-001-integer-prices.md` — why integer ticks, not floating point
- `docs/adr/ADR-002-intrusive-linked-list.md` — why intrusive doubly-linked list over `std::list`

**Why ADRs exist and how they differ from LEARNING.md:**

ADRs and LEARNING.md serve fundamentally different audiences and purposes:

- **ADRs** are *reference documents* — terse, one-page, four-section (Context / Decision / Alternatives / Consequences). Written for someone who already understands the codebase and wants to quickly look up *what* was decided and *why*. A recruiter spending 30 seconds on an ADR gets the decision and its rationale. An ADR doesn't teach — it records.

- **LEARNING.md** is a *tutorial* — written for someone who understands algorithms and C++ but hasn't built a systems project like this. It explains from first principles, gives detailed rationale, connects decisions to each other, and includes "check your understanding" prompts. It's long by design.

The same decision (e.g., "use integer prices") appears in both, but at different depths:
- ADR-001 says: "Prices are int64_t ticks. Floats violate determinism. Unsigned loses negative-spread calculations. Done."
- LEARNING.md (Task 2) says: "Here's *why* floats break determinism with a concrete IEEE 754 example, here's *why* signed over unsigned with a spread-calculation walkthrough, here's what this costs you at the CLI layer, here's how the strong-type wrapper works..."

Having both is deliberate: quick reference for experienced readers (ADRs) and deep explanation for learning (LEARNING.md). Neither replaces the other.

**Why this architecture:**
ADRs live in `docs/adr/` (not in `specs/` or inline in code) because they're cross-cutting decisions that outlive any single phase. "Integer prices" isn't a Phase 1 decision — it's a permanent project-wide constraint. Putting it in `specs/phase-01-order-book/` would imply it's scoped to that phase, which it isn't.

The README lives at the repo root because that's what GitHub renders by default — it's the project's front door. A recruiter cloning the repo sees it first.

**Complexity:** N/A — this task creates documentation, not algorithms.

**Benefits:**
1. **Recruiters get the "why" in 30 seconds:** ADRs are scannable reference pages. A hiring manager can open ADR-001 and immediately see "integer prices for determinism" without reading a 500-line tutorial
2. **README makes the project immediately buildable:** Anyone cloning the repo has working commands, a usage example, and pointers to deeper documentation
3. **Clear documentation hierarchy:** README (front door) → ADRs (quick reference) → LEARNING.md (deep tutorial) → design.md/requirements.md (full spec). Each has its audience and none duplicates another's role

**Drawbacks / tradeoffs accepted:**
1. **Some information repetition across documentation layers.** The same "why integer prices" reasoning appears in ADR-001, LEARNING.md Task 2, and design.md. This is acceptable because each serves a different reading context — you don't want to force a recruiter to read LEARNING.md just to understand a design choice
2. **ADRs are static snapshots.** If Phase 3 changes the price representation (unlikely, but hypothetically), the ADR must be updated or superseded. ADRs don't auto-update — they require manual maintenance

**How this connects to what came before:**
- Tasks 1–16 built the complete Phase 1 engine, tests, CLI, CI, and per-task LEARNING.md entries
- Task 17 wraps it all up: the README presents the completed work, ADRs crystallize the key decisions, and the project is ready for Phase 2

**Check your understanding:**
1. When would you create a *new* ADR vs. updating an existing one? (Hint: if Phase 3 switches from `std::map` to a flat sorted array for the price tree, is that a new ADR or an update to ADR-002?)
2. Why does ADR-001 mention "signed, not unsigned" for prices — what calculation produces a negative price-related value that unsigned would mishandle?
