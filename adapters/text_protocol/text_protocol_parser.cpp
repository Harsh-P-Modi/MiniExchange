#include "adapters/text_protocol/text_protocol_parser.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>
#include <vector>

namespace miniexchange::text_protocol {

namespace {

// Tokenize a line by whitespace into string_views pointing into the
// original buffer — zero allocation beyond the vector itself.
std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t pos = 0;
    while (pos < line.size()) {
        // Skip whitespace
        while (pos < line.size() &&
               (line[pos] == ' ' || line[pos] == '\t' ||
                line[pos] == '\r' || line[pos] == '\n')) {
            ++pos;
        }
        if (pos >= line.size()) {
            break;
        }
        // Find end of token
        std::size_t start = pos;
        while (pos < line.size() && line[pos] != ' ' &&
               line[pos] != '\t' && line[pos] != '\r' &&
               line[pos] != '\n') {
            ++pos;
        }
        tokens.push_back(line.substr(start, pos - start));
    }
    return tokens;
}

// Parse a side string ("BUY" or "SELL") into Side enum.
bool parse_side(std::string_view s, Side& out) {
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

// Parse a uint64 from a string_view using std::from_chars.
// Returns true on success (entire token consumed, no overflow).
bool parse_uint64(std::string_view s, uint64_t& out) {
    if (s.empty()) {
        return false;
    }
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

// Parse a signed int64 from a string_view using std::from_chars.
bool parse_int64(std::string_view s, int64_t& out) {
    if (s.empty()) {
        return false;
    }
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}

}  // namespace

ParseResult parse(std::string_view line) {
    auto tokens = tokenize(line);

    if (tokens.empty()) {
        return ParseError{"Empty command"};
    }

    const auto cmd = tokens[0];

    // CANCEL <id>
    if (cmd == "CANCEL") {
        if (tokens.size() != 2) {
            return ParseError{"Usage: CANCEL <id>"};
        }
        uint64_t id_val = 0;
        if (!parse_uint64(tokens[1], id_val)) {
            return ParseError{
                "Invalid order ID: " + std::string(tokens[1])};
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
            return ParseError{
                "Invalid order ID: " + std::string(tokens[1])};
        }
        Side side{};
        if (!parse_side(tokens[2], side)) {
            return ParseError{"Invalid side (expected BUY or SELL): " +
                              std::string(tokens[2])};
        }
        int64_t price_val = 0;
        if (!parse_int64(tokens[3], price_val)) {
            return ParseError{
                "Invalid price: " + std::string(tokens[3])};
        }
        uint64_t qty_val = 0;
        if (!parse_uint64(tokens[4], qty_val)) {
            return ParseError{
                "Invalid quantity: " + std::string(tokens[4])};
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
            return ParseError{
                "Invalid order ID: " + std::string(tokens[1])};
        }
        Side side{};
        if (!parse_side(tokens[2], side)) {
            return ParseError{"Invalid side (expected BUY or SELL): " +
                              std::string(tokens[2])};
        }
        uint64_t qty_val = 0;
        if (!parse_uint64(tokens[3], qty_val)) {
            return ParseError{
                "Invalid quantity: " + std::string(tokens[3])};
        }
        return MarketOrder{OrderId{id_val}, side, Quantity{qty_val}};
    }

    return ParseError{"Unknown command: " + std::string(cmd)};
}

}  // namespace miniexchange::text_protocol
