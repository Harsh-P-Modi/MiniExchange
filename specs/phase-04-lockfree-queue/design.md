# Phase 4 — Design: Lock-Free Queue

Status: **APPROVED** — `tasks.md` is written from this version.

## 1. Overview

A fixed-capacity, compile-time-sized SPSC ring buffer, plus a new
shared message type (`EngineCommand`) that both this queue and Phase
5's TCP gateway will carry — introducing it now, in `core/`, rather
than inventing a throwaway type just for this phase's tests.

## 2. `core/EngineCommand.hpp` (new — needed to test this queue with a
realistic payload, not just an `int`)

```cpp
struct CancelRequest { OrderId id; };

// The thing a producer thread hands to the engine thread: either a new
// order or a cancel. Flattened (not nested inside NewOrder) so a single
// std::visit dispatches directly to submit-limit / submit-market /
// cancel, without an extra unwrapping step.
using EngineCommand = std::variant<LimitOrder, MarketOrder, CancelRequest>;
```

**Cross-reference worth noting explicitly:** Phase 2's
`tools/workload_generator/WorkloadEvent` currently has its **own**
`struct CancelRequest { OrderId id; };` definition, in the same
`miniexchange` namespace this new `core/EngineCommand.hpp` uses. Once
this phase lands, that's a genuine redefinition conflict, not just
redundancy — Task 5 must **delete** `workload_generator.hpp`'s local
`CancelRequest` and `#include "core/EngineCommand.hpp"` instead of
merely adding an alias alongside the existing definition. "Alias
`WorkloadEvent` to `EngineCommand`" only works once there's exactly one
`CancelRequest` definition for both to share.

**Not a collision, despite the same name:** `apps/cli/`'s
`miniexchange::cli::CancelRequest` is a separate, deliberately app-local
type (it's part of the CLI's own command grammar alongside
`PrintBookRequest`/`QuitRequest`, which have no engine-level meaning).
Being in the `miniexchange::cli` namespace, not bare `miniexchange`,
it doesn't collide with `core/EngineCommand.hpp`'s `CancelRequest` — no
change needed there.

**Who dispatches an `EngineCommand`?** This phase only builds and
benchmarks the transport — it does not decide who calls `std::visit`
on a popped `EngineCommand` to route it to `EngineAPI::submit`/`cancel`.
That's Phase 5's responsibility (`apps/exchange_server`'s engine-thread
loop), since this phase has no consumer thread of its own beyond the
stress test's synthetic one. Stating this explicitly so it isn't left
as an implicit assumption a reader has to infer.

## 3. `lockfree_queue/SpscRingBuffer.hpp`

```cpp
template <typename T, size_t Capacity = 4096>
class alignas(64) SpscRingBuffer {   // alignas on the class itself, not
                                       // just the indices — otherwise
                                       // head_ could share a cache line
                                       // with whatever precedes this
                                       // object in memory (a stack frame,
                                       // a heap allocator's own header),
                                       // reintroducing the false-sharing
                                       // risk NFR2 exists to eliminate.
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
public:
    // Takes by value specifically so the caller's std::move (or a
    // temporary) is moved into storage_, not copied — try_push does
    // `storage_[tail_ & mask_] = std::move(item);`, never a copy.
    // Returns false if full (R4, resolved: reject, don't block).
    bool try_push(T item);

    // Moves the slot's value out: `out = std::move(storage_[head_ &
    // mask_]);`. The vacated slot still holds a moved-from T
    // afterward — not destroyed or reset. For EngineCommand (a
    // variant of trivially-copyable PODs) a moved-from state is
    // indistinguishable from its prior value and costs nothing to
    // leave lingering; this would need reconsidering for a T with
    // non-trivial destructors or held resources, which EngineCommand
    // isn't.
    // Returns false if empty (R3: non-blocking poll — the caller's
    // loop structure, e.g. spin or spin-then-yield, is not this
    // class's concern).
    bool try_pop(T& out);

    // Diagnostic only, not for correctness logic — an approximate
    // snapshot that can be stale the instant it's read, since the
    // producer/consumer may modify head_/tail_ concurrently with this
    // call. Fine for the benchmark's "average queue depth during the
    // run" reporting (Task 5); not fine as a precondition for any
    // push/pop decision, which must always go through try_push/try_pop
    // themselves. size() == tail_ - head_ needs no extra
    // synchronization beyond plain atomic loads — it's not read-modify-
    // write, so relaxed ordering is sufficient here (unlike try_push/
    // try_pop's acquire/release pairing, which protects the actual
    // data transfer, not a diagnostic count).
    size_t size() const {
        return tail_.value.load(std::memory_order_relaxed) -
               head_.value.load(std::memory_order_relaxed);
    }
    bool empty() const { return size() == 0; }

private:
    struct alignas(64) PaddedIndex {   // NFR2: each index gets its own
        std::atomic<size_t> value{0}; // cache line, so the producer
        char padding[64 - sizeof(std::atomic<size_t>)];  // writing tail_
    };                                                    // doesn't
                                                            // invalidate
                                                            // the consumer's
                                                            // cache line
                                                            // holding head_,
                                                            // and vice versa.
    PaddedIndex head_;   // next slot the consumer will read
    PaddedIndex tail_;   // next slot the producer will write
    std::array<T, Capacity> storage_;   // value-initializes all
                                         // Capacity slots at
                                         // construction — for
                                         // EngineCommand (a variant of
                                         // small PODs) this is cheap
                                         // (thousands of default
                                         // constructions, nanoseconds
                                         // total), but it's a real cost
                                         // being accepted, not an
                                         // oversight — a more
                                         // sophisticated design would
                                         // use aligned uninitialized
                                         // storage + placement-new to
                                         // avoid it, at the cost of
                                         // manual lifetime management
                                         // this project doesn't need
                                         // for a POD payload type.

    static constexpr size_t mask_ = Capacity - 1;  // index & mask_,
                                                     // not index % Capacity —
                                                     // free with power-of-two
};
```

**Memory ordering (NFR1), stated precisely, not just "it's atomic":**
- `try_push`: read `head_` with `memory_order_acquire` (need to see the
  consumer's most recent progress to know if there's space); write the
  item into `storage_[tail_ & mask_]` with plain (non-atomic) access;
  then store the incremented `tail_` with `memory_order_release`. The
  release-store on `tail_` is what makes the just-written item visible
  to the consumer's subsequent acquire-load of `tail_` — this is the
  release/acquire pairing that makes the plain write to `storage_`
  safe without its own atomic.
- `try_pop`: read `tail_` with `memory_order_acquire` (to see the
  producer's latest write and the data it published); read
  `storage_[head_ & mask_]` with plain access (safe because of the
  acquire above); then store the incremented `head_` with
  `memory_order_release` (so the producer's subsequent acquire-load of
  `head_` sees the freed slot).
- No `memory_order_seq_cst` anywhere — the acquire/release pairing on
  each index is sufficient because there is exactly one writer and one
  reader per index (`tail_` is only ever written by the producer, read
  by both; `head_` is only ever written by the consumer, read by both),
  so there's no need to establish a single global total order across
  more than two threads.

## 4. `apps/benchmark/MutexQueue.hpp` (app-local comparison baseline)

A `std::mutex` + `std::deque<T>` wrapper with the *same* `try_push`/
`try_pop` non-blocking-poll signature (no condition-variable wait, so
the comparison isolates "lock overhead" rather than conflating it with
"blocking vs. polling" as a separate variable) — lives in
`apps/benchmark/`, not `lockfree_queue/`, since it's a throwaway
comparison target, not part of the shipped architecture (matches the
`tools/` vs. app-local distinction from Phase 2: this isn't reused
anywhere else, so it doesn't earn a shared location).

## 5. Stress test (NFR3)

A dedicated producer thread pushes a known sequence (e.g. integers
1..1,000,000, or `EngineCommand`s tagged with a sequence number) while
a dedicated consumer thread pops and records what it received, then the
test asserts: nothing lost (received count == sent count), nothing
duplicated (no repeated sequence numbers), nothing reordered (received
in the exact order sent — true for a correct SPSC ring buffer, and a
good thing to explicitly assert since it's a real property, not just
"no crash"). Run under ThreadSanitizer as part of CI.

## 6. Why a ring buffer — not the other lock-free options

Per `.kiro/steering/tech.md`'s standing rule, the main data-
structure/algorithm choice gets an explicit "why not X" — this was
missing from the original draft and is worth stating plainly, since
it's exactly what an interviewer asks after "you wrote a lock-free SPSC
ring buffer":

- **Why not a Michael-Scott (linked, `std::atomic`-based) lock-free
  queue?** A linked queue allocates a node per element, which is
  precisely the per-order heap allocation Phase 3 just finished
  eliminating from the hot path — reintroducing it at the queue layer
  would undo that work one layer up. A ring buffer's fixed backing
  array means zero allocation after construction, for the entire
  lifetime of the queue.
- **Why not `moodycamel::ConcurrentQueue` (a well-known MPSC/MPMC
  library)?** It's a fine library, but it solves a harder problem
  (multiple producers) than this phase currently has (§ Open Questions,
  resolved: SPSC through Phase 8). Depending on it now would mean
  paying MPSC's inherent extra synchronization cost for a
  single-producer workload that doesn't need it — and it would mean
  reaching for a third-party dependency where a genuinely simple,
  from-scratch structure is both sufficient and more instructive to
  have built (this project's whole premise is demonstrating you
  understand the mechanism, not just that you can select a library).
- **Why not `boost::lockfree::spsc_queue` (why write your own at
  all)?** Same instructive-value argument, plus it avoids adding Boost
  as a dependency for one small, well-understood structure — this
  project already has a "new dependencies need a deliberate reason"
  bar (Phase 7 crosses it for `nlohmann/json`, because there's no
  reasonable from-scratch alternative for a fair JSON comparison; a
  ring buffer doesn't clear that bar, since writing one correctly is
  exactly the point).
- **Why not a SeqLock-based approach?** SeqLocks suit a different
  access pattern — many readers, one writer, all reading the *same*
  logical value repeatedly (e.g. a shared "latest price" cell), where
  readers detect and retry on a torn read. A queue's job is different:
  each element is consumed exactly once, not read repeatedly by
  multiple parties, so a SeqLock's retry-on-conflicting-write model
  doesn't map onto "hand off N distinct items, once each" the way a
  ring buffer's head/tail indices directly do.
- **Why a ring buffer specifically fits *this* problem well beyond just
  "not those":** sequential access into a fixed array is cache-friendly
  (the consumer's reads walk forward through memory it's likely to have
  already prefetched), the bounded capacity forces an explicit,
  visible back-pressure decision (R4) rather than unbounded memory
  growth under a slow consumer, and the whole structure is small enough
  to reason about and verify completely (§5's stress test can actually
  cover its full state space) — all properties that matter more here
  than raw peak throughput on a benchmark that doesn't reflect this
  project's actual bottleneck (single producer, single consumer, one
  hop).

## 7. Judgment calls made here — flag if any should change

1. **Compile-time `Capacity` template parameter, not a runtime
   constructor argument.** This gives the power-of-two `static_assert`
   real teeth (caught at compile time, not a runtime check that could
   be skipped) and lets `index & mask_` replace `index % Capacity` —
   free performance from a compile-time constant. The cost: different
   capacities require different template instantiations rather than
   one type configured at runtime. Given nothing in this project
   currently needs to *change* a queue's capacity after compiling, this
   trade favors compile-time.
2. **`std::array<T, Capacity>` storage**, not a heap-allocated
   `unique_ptr<T[]>` like Phase 3's `OrderPool`. Since `Capacity` is a
   compile-time constant here (unlike the pool's runtime-configurable
   size), `std::array` is the more direct fit and avoids an
   unnecessary heap allocation at construction.
3. **`EngineCommand` promoted to `core/`**, not left as a
   `lockfree_queue`-local or `tools/`-local type — it's a genuine
   domain-level concept (the set of things one may ask the engine to
   do) that Phase 5's production code needs too, not just this phase's
   tests.
4. **`std::array<T, Capacity>` value-initializes all slots up front**,
   rather than aligned uninitialized storage + placement-new. Accepted
   because `T = EngineCommand` is a small variant of PODs — the
   upfront cost is negligible and the code stays simpler. Would need
   revisiting if this template were ever instantiated with a
   heavier/non-trivial `T`, which nothing in this project currently
   does.

---

Once approved, `tasks.md` breaks this into steps: `EngineCommand` first
(small, standalone), then `SpscRingBuffer` itself with its stress test,
then the `MutexQueue` baseline, then the comparative benchmark, then
the Phase 2 `WorkloadEvent` alias tidy-up.
