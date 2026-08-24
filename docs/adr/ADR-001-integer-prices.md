# ADR-001: Integer Prices (Ticks, Not Floating Point)

**Status:** Accepted  
**Date:** Phase 1

## Context

A matching engine must represent prices for comparison, sorting (the
price tree), and trade execution. The two common options are
floating-point numbers (doubles) and fixed-width integers representing
discrete ticks.

Real exchanges (CME Globex, NASDAQ ITCH, LSE) use integer ticks
internally. Floating point introduces representation error (0.1 + 0.2
!= 0.3 in IEEE 754), which causes:
- Non-deterministic matching (prices that should be equal compare as
  unequal)
- Subtle bugs in "is this price better?" comparisons
- Rounding-dependent behavior that differs across compilers/platforms

This project's charter mandates deterministic execution — same input
sequence always produces the same output sequence.

## Decision

Prices are `int64_t` ticks everywhere in `core/`, `orderbook/`, and
`engine/`. No floating-point types appear in these modules. Period.

`Price` is a strong wrapper struct (`struct Price { int64_t value; }`)
providing comparison and arithmetic operators. Signed, because
spread calculations (bid - ask) can be negative.

Any conversion from human-readable prices (e.g., "$100.25" →
tick 10025) is a presentation concern that lives in `apps/` or
`adapters/`, never in the engine.

## Alternatives Considered

1. **`double`** — simpler for the CLI layer (no tick conversion), but
   introduces IEEE 754 representation error. Comparison (`==`) is
   unreliable. Sorting is non-deterministic across platforms.
   Rejected: violates determinism requirement.

2. **Fixed-point decimal (`int64_t` with implicit scale, e.g.,
   value / 10000)** — gives human-readable prices without floats.
   Rejected as unnecessary complexity: the engine doesn't need to know
   what a tick "means" in dollars. Scale is a display concern. Raw
   integer ticks are simpler and equally correct.

3. **`uint64_t` (unsigned)** — saves one bit of range. Rejected
   because spread/delta calculations produce negative values, and
   unsigned subtraction wraps silently (undefined territory for
   correctness).

## Consequences

- **Positive:** Exact comparison (`==`, `<`, `>`) is bitwise correct.
  Deterministic matching across all compilers and platforms. Zero risk
  of rounding bugs. Cache-friendly (8 bytes, no padding).
- **Negative:** The CLI must parse `"10020"` as an integer (trivial).
  If a future phase needs sub-tick granularity, the tick size must be
  scaled up front (not a Phase 1 concern).
- **Enforced by:** `.kiro/steering/tech.md` hard rule; `static_assert`
  in `core/Types.hpp` confirms no float conversions compile.
