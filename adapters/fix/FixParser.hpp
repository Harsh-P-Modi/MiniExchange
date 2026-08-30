#ifndef MINIEXCHANGE_ADAPTERS_FIX_FIX_PARSER_HPP
#define MINIEXCHANGE_ADAPTERS_FIX_FIX_PARSER_HPP

#include <cstdint>
#include <string_view>
#include <variant>

#include "adapters/fix/FixError.hpp"
#include "adapters/fix/FixMessage.hpp"
#include "core/EngineCommand.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"

namespace miniexchange::fix {

// The single symbol this exchange supports (Phase 1 scope). A 35=D whose
// tag 55 (Symbol) doesn't match is rejected with UnsupportedSymbol rather
// than silently accepted (R1: clean rejection, not silent handling).
inline constexpr std::string_view kSupportedSymbol = "MINI";

// ParsedMessage — the successful output of parsing a business message:
// either a NewOrder to submit, or a CancelRequest carrying the OrderId to
// cancel. (CancelRequest is Phase 4's core type: a wrapper around OrderId.)
using ParsedMessage = std::variant<NewOrder, CancelRequest>;

// FixParser — maps a validated TagMap to an engine command.
//
// Stateless with respect to order correlation: because ClOrdID (tag 11)
// is numeric-only and equals OrderId directly (design.md Q1/Q3), a cancel
// references its target OrderId directly via tag 41/11 — no ClOrdID<->
// OrderId table is needed.
//
// owner: a NewOrder built from a 35=D needs a ClientId owner (Phase 8's
// retrofit). The parser accepts the owner as a parameter — the caller
// (the FIX session layer / gateway) is responsible for mapping the FIX
// session's SenderCompID to a ClientId (see FixSession). This keeps the
// parser itself free of session state.
class FixParser {
public:
    // Parse a validated TagMap into a NewOrder or CancelRequest.
    // `owner` is stamped onto any NewOrder produced (35=D). It is ignored
    // for 35=F (cancel), which carries no owner.
    static std::variant<ParsedMessage, FixError> parse(const TagMap& map,
                                                        ClientId owner);

private:
    static std::variant<ParsedMessage, FixError> parse_new_order_single(
        const TagMap& map, ClientId owner);
    static std::variant<ParsedMessage, FixError> parse_cancel_request(
        const TagMap& map);
};

// Parse a numeric FIX field into a uint64_t. Returns false if the value
// is absent or not a clean non-negative integer.
bool parse_uint64_field(const TagMap& map, int tag, uint64_t& out);

}  // namespace miniexchange::fix

#endif  // MINIEXCHANGE_ADAPTERS_FIX_FIX_PARSER_HPP
