#ifndef MINIEXCHANGE_CORE_ENGINE_COMMAND_HPP
#define MINIEXCHANGE_CORE_ENGINE_COMMAND_HPP

#include "NewOrder.hpp"
#include "Types.hpp"
#include <variant>

namespace miniexchange {

// CancelRequest — the canonical engine-facing representation of a cancel
// command. Distinct from apps/cli/'s miniexchange::cli::CancelRequest
// (which is part of the CLI's own command grammar alongside PrintBookRequest
// and QuitRequest — app-local types with no engine-level meaning).
//
// This struct exists so EngineCommand's variant can distinguish "cancel
// this order" from "submit this new order" via std::visit, without the
// consumer needing a separate code path or a type-erased tag enum.
struct CancelRequest {
    OrderId id;
};

// EngineCommand — the single message type carried across the SPSC ring
// buffer from a producer thread (Phase 5's TCP gateway, benchmark harness,
// etc.) to the matching-engine thread.
//
// Flattened: LimitOrder, MarketOrder, and CancelRequest are all top-level
// alternatives, not nested inside a wrapper. This means a single
// std::visit dispatches directly to EngineAPI::submit (limit),
// EngineAPI::submit (market), or EngineAPI::cancel — no extra unwrapping
// step, no intermediate NewOrder construction.
//
// Why std::variant over a tagged union or inheritance hierarchy:
// - Compile-time exhaustiveness checking (the compiler warns if a visitor
//   doesn't handle all alternatives).
// - Value semantics (no heap allocation, no pointer indirection — critical
//   for the ring buffer's cache-friendly sequential storage).
// - The alternatives are small PODs (16–32 bytes each); the variant's
//   overhead (one size_t tag + max-alternative-sized storage) is negligible.
using EngineCommand = std::variant<LimitOrder, MarketOrder, CancelRequest>;

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_ENGINE_COMMAND_HPP
