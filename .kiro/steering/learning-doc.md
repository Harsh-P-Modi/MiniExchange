# Learning Documentation Policy

## Why this file exists

Harsh is directing this project's specs and reviewing its output, but
Kiro is writing the implementation code. That gap is fine for shipping
a portfolio piece — it is not fine for actually understanding a
matching engine well enough to defend it in an HFT interview. This
steering file exists to close that gap. It is a standing instruction,
not a one-time task: it applies to every phase, every session, forever.

## The deliverable

Maintain a single running file: **`docs/LEARNING.md`** at the repo
root's `docs/` folder (create it if it doesn't exist). This is separate
from:
- `docs/adr/` — terse, one-page Architecture Decision Records (Context
  / Decision / Alternatives / Consequences). ADRs are a *reference*,
  written for someone who already understands the codebase.
- `LEARNING.md` — a *tutorial*, written for Harsh specifically, at the
  depth of someone who understands competitive-programming-level C++
  and algorithms well, but has not built a systems project like this
  before and did not personally write this code.

`LEARNING.md` may be long. That is expected and fine — do not compress
it for the sake of brevity. Length is not a cost here; an unexplained
gap is.

## When to update it

After **every** implementation task that produces or meaningfully
changes code (not after every trivial edit, but after anything a
`tasks.md` entry would call a discrete step) — append to `LEARNING.md`,
never overwrite prior sections. Treat it as a living document that
grows with the project, structured with one top-level section per
phase and sub-sections per module/class introduced or changed in that
phase.

**This is bundled into the same task, not a second review cycle.** A
task's review looks at the code diff *and* its `LEARNING.md` addition
together, in one pass — it is not "finish the code, get it approved,
then separately write and get approval for documentation." If a task
introduces one small, obvious type, its `LEARNING.md` entry can be
correspondingly short (the ten items in the next section are what to
*cover*, not a minimum word count each). The cost of this policy should
scale with how much genuinely new ground a task covers, not multiply
the number of stop-and-wait points in `tasks.md` — Phase 1 having 17
tasks means 17 `LEARNING.md` additions bundled into those same 17
reviews, not 34 separate checkpoints.

**Depth also scales with the task, not a fixed ten-item minimum every
time.** For a genuinely trivial task (e.g. a plain type alias with no
real alternative worth naming), an item like "alternatives considered"
can be a single honest sentence ("no real alternative — a bare
`uint64_t` directly would lose type-safety for no benefit") rather than
a forced paragraph. The ten items below are what to *check against*,
not a template to fill out uniformly regardless of how much there
actually is to say. The one thing that's never acceptable is *skipping*
an item because the task seemed simple (see "What NOT to do" below) —
brief-and-present is fine; absent is not.

## What each entry must cover

For every significant type, class, function, or algorithm introduced,
write a sub-section answering all of the following — skipping none of
them, even if the answer feels obvious:

1. **What it does**, in plain language first, before any code.
2. **The exact location**: file path and line range where it's
   implemented (e.g. `orderbook/PriceLevel.hpp:34–52`). Update this
   reference if the code moves. If a concept is spread across several
   places (e.g. a struct definition plus its usage in the matching
   loop), cite all of the relevant locations, not just the first one.
3. **Why this data structure / algorithm, specifically** — not just
   "we used an intrusive doubly-linked list" but *why that beats*
   `std::list`, a plain vector, a `std::map`-based approach, etc., for
   this exact use case. Reference the actual reasoning, not a generic
   textbook justification.
4. **Why this architecture / pattern** where relevant — e.g. why this
   lives in `orderbook/` and not `engine/`, why it's reached through a
   port instead of a direct dependency, why it's an app vs. an adapter.
5. **Complexity** — time and space, stated explicitly (e.g. "O(1)
   amortized for cancel, because...").
6. **Benefits** of the choice made.
7. **Drawbacks / known issues / tradeoffs accepted** — every design
   decision costs something; say what.
8. **Alternatives that were considered and rejected**, and why they
   lost out specifically for this project (not in the abstract).
9. **How this connects to what came before** — if this phase's code
   depends on or modifies something from an earlier phase, say so
   explicitly and link back to that earlier section.
10. **A short "check your understanding" prompt** — one or two
    questions Harsh could ask himself (or be asked in an interview) to
    verify he actually understands this piece, e.g. "Why would this
    break if `Order` didn't embed its own `prev`/`next` pointers?" No
    answer needs to be given — the question itself is the point.

## Tone and audience

Write as a patient mentor explaining to a strong competitive
programmer who is new to systems/HFT-style engineering — not as a
formal spec, not as marketing copy, not as a terse comment. Assume
familiarity with algorithms, complexity analysis, and general C++, but
not with intrusive containers, lock-free structures, hexagonal
architecture, or exchange internals specifically — those get explained
from first principles the first time they appear, with a pointer back
to that explanation on reuse rather than re-explaining every time.

## What NOT to do

- Do not write `LEARNING.md` entries that just restate the `design.md`
  prose more verbosely — `design.md` is what was decided; `LEARNING.md`
  is why it's correct and what it costs, taught from scratch.
- Do not skip a module because it "seems simple" — simple things
  (e.g. why `Price` is a signed integer, not unsigned) are exactly what
  gets glossed over and then misunderstood later.
- Do not let this file go stale relative to the code. If a later phase
  changes something documented earlier (e.g. swapping `std::map` for a
  flat array in Phase 3), update the earlier section to reflect the
  change and explain the delta — don't just add a new section that
  contradicts the old one silently.
