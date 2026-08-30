#ifndef MINIEXCHANGE_ADAPTERS_FIX_FIX_ERROR_HPP
#define MINIEXCHANGE_ADAPTERS_FIX_FIX_ERROR_HPP

#include <string>

namespace miniexchange::fix {

// FixErrorReason — a specific, loggable reason for every rejection path
// in the FIX parser (design.md §5, requirements.md NFR1). FIX debugging
// in practice depends on knowing exactly which tag was the problem, so
// every failure maps to a distinct reason rather than a generic
// "parse failed".
enum class FixErrorReason {
    ChecksumMismatch,        // tag 10 doesn't match the computed checksum
    BodyLengthMismatch,      // tag 9 doesn't match the actual body length
    MalformedField,          // a field had no '=' or an empty tag
    EmptyMessage,            // nothing to parse
    InvalidClOrdIdFormat,    // tag 11 (or 41) not a numeric uint64
    UnsupportedSymbol,       // tag 55 not the single supported symbol
    InvalidSide,             // tag 54 not 1 (Buy) or 2 (Sell)
    UnsupportedOrdType,      // tag 40 not 1 (Market) or 2 (Limit)
    MissingPrice,            // tag 44 absent on a Limit order
    InvalidQuantity,         // tag 38 absent, non-numeric, or non-positive
    UnsupportedMessageType,  // tag 35 not D or F
    MissingRequiredTag,      // a required tag was absent (see offending_tag)
};

// FixError — a rejection with enough context to log and debug.
struct FixError {
    FixErrorReason reason;
    int offending_tag = -1;  // the FIX tag at fault, or -1 if not
                             // tag-specific (e.g. ChecksumMismatch)
    std::string detail;      // human-readable, loggable description
};

// Human-readable name for a reason (for logs / test diagnostics).
inline const char* to_string(FixErrorReason r) {
    switch (r) {
        case FixErrorReason::ChecksumMismatch:       return "ChecksumMismatch";
        case FixErrorReason::BodyLengthMismatch:     return "BodyLengthMismatch";
        case FixErrorReason::MalformedField:         return "MalformedField";
        case FixErrorReason::EmptyMessage:           return "EmptyMessage";
        case FixErrorReason::InvalidClOrdIdFormat:   return "InvalidClOrdIdFormat";
        case FixErrorReason::UnsupportedSymbol:      return "UnsupportedSymbol";
        case FixErrorReason::InvalidSide:            return "InvalidSide";
        case FixErrorReason::UnsupportedOrdType:     return "UnsupportedOrdType";
        case FixErrorReason::MissingPrice:           return "MissingPrice";
        case FixErrorReason::InvalidQuantity:        return "InvalidQuantity";
        case FixErrorReason::UnsupportedMessageType: return "UnsupportedMessageType";
        case FixErrorReason::MissingRequiredTag:     return "MissingRequiredTag";
    }
    return "Unknown";
}

}  // namespace miniexchange::fix

#endif  // MINIEXCHANGE_ADAPTERS_FIX_FIX_ERROR_HPP
