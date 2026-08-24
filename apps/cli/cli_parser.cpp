#include "apps/cli/cli_parser.hpp"

#include <charconv>
#include <sstream>
#include <string>
#include <vector>

namespace miniexchange::cli {

namespace {

// Tokenize a line by whitespace. Returns empty vector for blank lines.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

// Parse a side string ("BUY" or "SELL") into Side enum.
// Returns true on success, false on failure.
bool parse_side(const std::string& s, Side& out) {
    if (s == "BUY") {
        out = Side::Buy;
        return true;
    }
    if (s == "SELL") {
        out = Side::Sell;
        return true;
    }
    return false;
}

// Parse a uint64 from a string using std::from_chars.
// Returns true on success (entire string consumed, no overflow).
bool parse_uint64(const std::string& s, uint64_t& out) {
    if (s.empty()) {
        return false;
    }
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

// Parse a signed int64 from a string using std::from_chars.
bool parse_int64(const std::string& s, int64_t& out) {
    if (s.empty()) {
        return false;
    }
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

}  // namespace

ParseResult CLIParser::parse(const std::string& line) const {
    auto tokens = tokenize(line);

    if (tokens.empty()) {
        return ParseError{"Empty command"};
    }

    const auto& cmd = tokens[0];

    // QUIT
    if (cmd == "QUIT") {
        if (tokens.size() != 1) {
            return ParseError{"QUIT takes no arguments"};
        }
        return QuitRequest{};
    }

    // PRINT_BOOK
    if (cmd == "PRINT_BOOK") {
        if (tokens.size() != 1) {
            return ParseError{"PRINT_BOOK takes no arguments"};
        }
        return PrintBookRequest{};
    }

    // CANCEL <id>
    if (cmd == "CANCEL") {
        if (tokens.size() != 2) {
            return ParseError{"Usage: CANCEL <id>"};
        }
        uint64_t id_val = 0;
        if (!parse_uint64(tokens[1], id_val)) {
            return ParseError{"Invalid order ID: " + tokens[1]};
        }
        return CancelRequest{OrderId{id_val}};
    }

    // ADD <id> <BUY|SELL> <price> <qty>
    if (cmd == "ADD") {
        if (tokens.size() != 5) {
            return ParseError{"Usage: ADD <id> <BUY|SELL> <price> <qty>"};
        }
        uint64_t id_val = 0;
        if (!parse_uint64(tokens[1], id_val)) {
            return ParseError{"Invalid order ID: " + tokens[1]};
        }
        Side side{};
        if (!parse_side(tokens[2], side)) {
            return ParseError{"Invalid side (expected BUY or SELL): " + tokens[2]};
        }
        int64_t price_val = 0;
        if (!parse_int64(tokens[3], price_val)) {
            return ParseError{"Invalid price: " + tokens[3]};
        }
        uint64_t qty_val = 0;
        if (!parse_uint64(tokens[4], qty_val)) {
            return ParseError{"Invalid quantity: " + tokens[4]};
        }
        return LimitOrder{OrderId{id_val}, side, Price{price_val},
                          Quantity{qty_val}};
    }

    // MARKET <id> <BUY|SELL> <qty>
    if (cmd == "MARKET") {
        if (tokens.size() != 4) {
            return ParseError{"Usage: MARKET <id> <BUY|SELL> <qty>"};
        }
        uint64_t id_val = 0;
        if (!parse_uint64(tokens[1], id_val)) {
            return ParseError{"Invalid order ID: " + tokens[1]};
        }
        Side side{};
        if (!parse_side(tokens[2], side)) {
            return ParseError{"Invalid side (expected BUY or SELL): " + tokens[2]};
        }
        uint64_t qty_val = 0;
        if (!parse_uint64(tokens[3], qty_val)) {
            return ParseError{"Invalid quantity: " + tokens[3]};
        }
        return MarketOrder{OrderId{id_val}, side, Quantity{qty_val}};
    }

    return ParseError{"Unknown command: " + cmd};
}

}  // namespace miniexchange::cli
