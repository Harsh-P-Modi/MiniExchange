#include "adapters/fix/FixParser.hpp"

#include <charconv>
#include <cstdint>

namespace miniexchange::fix {

bool parse_uint64_field(const TagMap& map, int tag, uint64_t& out) {
    auto v = map.get(tag);
    if (!v.has_value() || v->empty()) return false;
    uint64_t parsed = 0;
    const char* begin = v->data();
    const char* end = begin + v->size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) return false;
    out = parsed;
    return true;
}

// Parse a signed integer FIX field (prices can in principle be negative
// in the type system, though the engine rejects non-positive prices).
static bool parse_int64_field(const TagMap& map, int tag, int64_t& out) {
    auto v = map.get(tag);
    if (!v.has_value() || v->empty()) return false;
    int64_t parsed = 0;
    const char* begin = v->data();
    const char* end = begin + v->size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) return false;
    out = parsed;
    return true;
}

std::variant<ParsedMessage, FixError> FixParser::parse(const TagMap& map,
                                                       ClientId owner) {
    auto msg_type = map.get(35);
    if (!msg_type.has_value()) {
        return FixError{FixErrorReason::MissingRequiredTag, 35,
                        "missing MsgType (tag 35)"};
    }
    if (*msg_type == "D") {
        return parse_new_order_single(map, owner);
    }
    if (*msg_type == "F") {
        return parse_cancel_request(map);
    }
    // R1: any other message type is rejected cleanly, not silently
    // ignored.
    return FixError{FixErrorReason::UnsupportedMessageType, 35,
                    "only 35=D and 35=F are supported"};
}

std::variant<ParsedMessage, FixError> FixParser::parse_new_order_single(
    const TagMap& map, ClientId owner) {
    // Tag 11 (ClOrdID) -> OrderId, numeric-only (Q1).
    uint64_t id_val = 0;
    if (!parse_uint64_field(map, 11, id_val)) {
        return FixError{FixErrorReason::InvalidClOrdIdFormat, 11,
                        "ClOrdID (tag 11) must be a numeric uint64"};
    }

    // Tag 55 (Symbol) validated against the single supported symbol.
    auto sym = map.get(55);
    if (!sym.has_value()) {
        return FixError{FixErrorReason::MissingRequiredTag, 55,
                        "missing Symbol (tag 55)"};
    }
    if (*sym != kSupportedSymbol) {
        return FixError{FixErrorReason::UnsupportedSymbol, 55,
                        "symbol not supported (single-symbol exchange)"};
    }

    // Tag 54 (Side): 1=Buy, 2=Sell (FIX 4.2).
    auto side_sv = map.get(54);
    if (!side_sv.has_value()) {
        return FixError{FixErrorReason::MissingRequiredTag, 54,
                        "missing Side (tag 54)"};
    }
    Side side;
    if (*side_sv == "1") {
        side = Side::Buy;
    } else if (*side_sv == "2") {
        side = Side::Sell;
    } else {
        return FixError{FixErrorReason::InvalidSide, 54,
                        "Side (tag 54) must be 1 (Buy) or 2 (Sell)"};
    }

    // Tag 38 (OrderQty): required, positive.
    uint64_t qty_val = 0;
    if (!parse_uint64_field(map, 38, qty_val) || qty_val == 0) {
        return FixError{FixErrorReason::InvalidQuantity, 38,
                        "OrderQty (tag 38) must be a positive integer"};
    }

    // Tag 40 (OrdType): 1=Market, 2=Limit.
    auto ordtype = map.get(40);
    if (!ordtype.has_value()) {
        return FixError{FixErrorReason::MissingRequiredTag, 40,
                        "missing OrdType (tag 40)"};
    }
    if (*ordtype == "2") {
        // Limit — tag 44 (Price) required.
        int64_t price_val = 0;
        if (!parse_int64_field(map, 44, price_val)) {
            return FixError{FixErrorReason::MissingPrice, 44,
                            "Limit order (OrdType=2) requires Price (tag 44)"};
        }
        LimitOrder lo{OrderId{id_val}, side, Price{price_val},
                      Quantity{qty_val}};
        lo.owner = owner;
        return ParsedMessage{NewOrder{lo}};
    }
    if (*ordtype == "1") {
        // Market — no price.
        MarketOrder mo{OrderId{id_val}, side, Quantity{qty_val}};
        mo.owner = owner;
        return ParsedMessage{NewOrder{mo}};
    }
    return FixError{FixErrorReason::UnsupportedOrdType, 40,
                    "OrdType (tag 40) must be 1 (Market) or 2 (Limit)"};
}

std::variant<ParsedMessage, FixError> FixParser::parse_cancel_request(
    const TagMap& map) {
    // Q3: prefer tag 41 (OrigClOrdID); fall back to tag 11 (ClOrdID).
    // Both are numeric-only and equal the OrderId directly (Q1), so no
    // correlation table is needed.
    uint64_t id_val = 0;
    if (parse_uint64_field(map, 41, id_val)) {
        return ParsedMessage{CancelRequest{OrderId{id_val}}};
    }
    if (parse_uint64_field(map, 11, id_val)) {
        return ParsedMessage{CancelRequest{OrderId{id_val}}};
    }
    // Distinguish "present but non-numeric" from "absent" for a better
    // error: if either tag exists but failed to parse, it's a format
    // problem; otherwise it's a missing required tag.
    if (map.has(41) || map.has(11)) {
        return FixError{FixErrorReason::InvalidClOrdIdFormat, 41,
                        "OrigClOrdID/ClOrdID must be a numeric uint64"};
    }
    return FixError{FixErrorReason::MissingRequiredTag, 41,
                    "cancel requires OrigClOrdID (tag 41) or ClOrdID (tag 11)"};
}

}  // namespace miniexchange::fix
