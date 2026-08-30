#ifndef MINIEXCHANGE_ADAPTERS_FIX_FIX_MESSAGE_HPP
#define MINIEXCHANGE_ADAPTERS_FIX_FIX_MESSAGE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "adapters/fix/FixError.hpp"

namespace miniexchange::fix {

// The FIX field separator is SOH (Start of Heading), byte 0x01 — NOT a
// printable pipe. Human-readable dumps use '|' but the wire uses SOH.
inline constexpr char kSOH = '\x01';

// FIX 4.2 BeginString value (tag 8).
inline constexpr std::string_view kBeginString = "FIX.4.2";

// TagMap — an ordered list of (tag, value) fields as they appeared on the
// wire. FIX permits repeated tags and order can be significant for some
// tags, so we keep insertion order and expose a first-match lookup rather
// than collapsing into an unordered map. For our 3-message subset there
// are no repeating groups, so first-match is the correct semantics.
class TagMap {
public:
    void add(int tag, std::string_view value) {
        fields_.push_back({tag, std::string(value)});
    }

    // First value for `tag`, or nullopt if absent.
    [[nodiscard]] std::optional<std::string_view> get(int tag) const {
        for (const auto& f : fields_) {
            if (f.first == tag) return std::string_view(f.second);
        }
        return std::nullopt;
    }

    [[nodiscard]] bool has(int tag) const { return get(tag).has_value(); }

    [[nodiscard]] const std::vector<std::pair<int, std::string>>& fields() const {
        // Exposed for tests / diagnostics.
        return fields_;
    }

private:
    std::vector<std::pair<int, std::string>> fields_;
};

// FixMessage — the mechanical envelope layer. It tokenizes SOH-delimited
// tag=value fields and validates the two structural envelope tags
// (BodyLength tag 9, CheckSum tag 10) BEFORE any business-tag meaning is
// assigned. This keeps the purely-mechanical envelope validation
// independent of message-type parsing (design.md §2).
class FixMessage {
public:
    // Tokenize raw SOH-delimited bytes into a TagMap. Does NOT validate
    // checksum/bodylength or interpret any business tag — only structure.
    // Returns MalformedField / EmptyMessage on structural problems.
    static std::variant<TagMap, FixError> tokenize(std::string_view raw);

    // Tokenize AND validate the envelope (BodyLength tag 9, CheckSum tag
    // 10) per the FIX spec. This is the entry point a real gateway would
    // use — a malformed envelope never reaches business parsing.
    static std::variant<TagMap, FixError> parse_validated(std::string_view raw);

    // Compute the FIX checksum: sum of all bytes up to (but not
    // including) the "10=" checksum field, modulo 256. Exposed so tests
    // can compute expected checksums independently of the encoder.
    static uint8_t compute_checksum(std::string_view bytes_before_checksum);

    // Compute FIX BodyLength: the number of bytes from immediately after
    // the BodyLength field's SOH, up to and including the SOH just before
    // the CheckSum field (tag 10). Exposed for the encoder and for tests.
    static std::size_t compute_body_length(std::string_view full_message);
};

}  // namespace miniexchange::fix

#endif  // MINIEXCHANGE_ADAPTERS_FIX_FIX_MESSAGE_HPP
