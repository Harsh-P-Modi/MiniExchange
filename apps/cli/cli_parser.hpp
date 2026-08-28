#ifndef MINIEXCHANGE_APPS_CLI_CLI_PARSER_HPP
#define MINIEXCHANGE_APPS_CLI_CLI_PARSER_HPP

#include <string>
#include <variant>

#include "core/EngineCommand.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"

namespace miniexchange::cli {

// CLI-only commands — these never reach the engine and are handled
// entirely within the CLI's main loop.
struct PrintBookRequest {};

struct QuitRequest {};

struct ParseError {
    std::string message;
};

// ParseResult — discriminated union of all possible CLI parse outcomes.
//
// Engine-facing commands (LimitOrder, MarketOrder, CancelRequest) come
// from the shared text_protocol parser; CLI-only commands (PrintBookRequest,
// QuitRequest) are intercepted locally before delegation.
using ParseResult = std::variant<LimitOrder, MarketOrder, CancelRequest,
                                 PrintBookRequest, QuitRequest, ParseError>;

// CLIParser — stateless line-by-line parser for the CLI grammar.
//
// Grammar (requirements.md §7):
//   ADD <id> BUY <price> <qty>
//   ADD <id> SELL <price> <qty>
//   MARKET <id> BUY <qty>
//   MARKET <id> SELL <qty>
//   CANCEL <id>
//   PRINT_BOOK
//   QUIT
//
// Intercepts PRINT_BOOK and QUIT locally, delegates everything else to
// adapters/text_protocol/text_protocol_parser.hpp's parse() function.
// Returns a ParseResult variant. ParseError carries a human-readable
// message for malformed input. No exceptions for bad input.
class CLIParser {
public:
    [[nodiscard]] ParseResult parse(const std::string& line) const;
};

}  // namespace miniexchange::cli

#endif  // MINIEXCHANGE_APPS_CLI_CLI_PARSER_HPP
