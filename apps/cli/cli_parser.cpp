#include "apps/cli/cli_parser.hpp"

#include <string>
#include <string_view>
#include <variant>

#include "adapters/text_protocol/text_protocol_parser.hpp"

namespace miniexchange::cli {

namespace {

// Skip leading whitespace and return the first whitespace-delimited
// token, or an empty view if the line is blank.
std::string_view first_token(std::string_view line) {
    std::size_t pos = 0;
    while (pos < line.size() &&
           (line[pos] == ' ' || line[pos] == '\t')) {
        ++pos;
    }
    if (pos >= line.size()) {
        return {};
    }
    std::size_t start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
        ++pos;
    }
    return line.substr(start, pos - start);
}

// Check that the line contains exactly one token (no trailing args).
bool is_single_token(std::string_view line) {
    std::size_t pos = 0;
    // Skip leading whitespace
    while (pos < line.size() &&
           (line[pos] == ' ' || line[pos] == '\t')) {
        ++pos;
    }
    // Skip the token itself
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
        ++pos;
    }
    // Skip trailing whitespace
    while (pos < line.size() &&
           (line[pos] == ' ' || line[pos] == '\t')) {
        ++pos;
    }
    return pos >= line.size();
}

}  // namespace

ParseResult CLIParser::parse(const std::string& line) const {
    auto cmd = first_token(line);

    // CLI-only commands: intercept before delegating.
    if (cmd == "QUIT") {
        if (!is_single_token(line)) {
            return ParseError{"QUIT takes no arguments"};
        }
        return QuitRequest{};
    }

    if (cmd == "PRINT_BOOK") {
        if (!is_single_token(line)) {
            return ParseError{"PRINT_BOOK takes no arguments"};
        }
        return PrintBookRequest{};
    }

    // Everything else: delegate to the shared text protocol parser.
    auto result = text_protocol::parse(line);

    // Convert text_protocol::ParseResult into cli::ParseResult.
    return std::visit(
        [](auto&& val) -> ParseResult {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, text_protocol::ParseError>) {
                return cli::ParseError{std::move(val.message)};
            } else {
                // LimitOrder, MarketOrder, CancelRequest pass through
                // directly — they're the same types in both variants.
                return val;
            }
        },
        std::move(result));
}

}  // namespace miniexchange::cli
