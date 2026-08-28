#ifndef MINIEXCHANGE_ADAPTERS_TEXT_PROTOCOL_PARSER_HPP
#define MINIEXCHANGE_ADAPTERS_TEXT_PROTOCOL_PARSER_HPP

#include <string>
#include <string_view>
#include <variant>

#include "core/EngineCommand.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"

namespace miniexchange::text_protocol {

// ParseError — protocol-level parse failure. Distinct from
// apps/cli/'s ParseError (that one includes CLI-only commands like
// PRINT_BOOK/QUIT in its grammar; this one doesn't).
struct ParseError {
    std::string message;
};

// ParseResult — the subset of commands that reach the engine.
// PRINT_BOOK and QUIT are CLI-only and produce a ParseError here.
using ParseResult =
    std::variant<LimitOrder, MarketOrder, CancelRequest, ParseError>;

// Parses a single line of text protocol grammar into an engine command
// or error. Zero-copy: takes string_view for direct use from TCP
// buffers without requiring a std::string copy.
//
// Grammar (shared between CLI and TCP gateway):
//   ADD <id> BUY <price> <qty>
//   ADD <id> SELL <price> <qty>
//   MARKET <id> BUY <qty>
//   MARKET <id> SELL <qty>
//   CANCEL <id>
//
// Returns ParseError for malformed input or unrecognized commands.
[[nodiscard]] ParseResult parse(std::string_view line);

}  // namespace miniexchange::text_protocol

#endif  // MINIEXCHANGE_ADAPTERS_TEXT_PROTOCOL_PARSER_HPP
