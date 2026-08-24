#ifndef MINIEXCHANGE_APPS_CLI_CLI_PARSER_HPP
#define MINIEXCHANGE_APPS_CLI_CLI_PARSER_HPP

#include <string>
#include <variant>

#include "core/NewOrder.hpp"
#include "core/Types.hpp"

namespace miniexchange::cli {

// Parsed command types — the parser turns a raw text line into one of these.
struct CancelRequest {
    OrderId id;
};

struct PrintBookRequest {};

struct QuitRequest {};

struct ParseError {
    std::string message;
};

// ParseResult — discriminated union of all possible parse outcomes.
// The CLI main loop dispatches on which alternative is held.
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
// Returns a ParseResult variant. ParseError carries a human-readable
// message for malformed input. No exceptions for bad input.
class CLIParser {
public:
    [[nodiscard]] ParseResult parse(const std::string& line) const;
};

}  // namespace miniexchange::cli

#endif  // MINIEXCHANGE_APPS_CLI_CLI_PARSER_HPP
