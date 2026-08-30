#include "adapters/fix/FixMessage.hpp"

#include <charconv>
#include <cstdio>

namespace miniexchange::fix {

std::variant<TagMap, FixError> FixMessage::tokenize(std::string_view raw) {
    if (raw.empty()) {
        return FixError{FixErrorReason::EmptyMessage, -1,
                        "empty FIX message"};
    }

    TagMap map;
    std::size_t pos = 0;
    while (pos < raw.size()) {
        // Find the next SOH.
        std::size_t soh = raw.find(kSOH, pos);
        std::string_view field;
        if (soh == std::string_view::npos) {
            // Trailing bytes with no terminating SOH. FIX requires every
            // field (including the last, tag 10) to be SOH-terminated, so
            // this is malformed.
            return FixError{FixErrorReason::MalformedField, -1,
                            "final field not SOH-terminated"};
        }
        field = raw.substr(pos, soh - pos);
        pos = soh + 1;

        if (field.empty()) {
            return FixError{FixErrorReason::MalformedField, -1,
                            "empty field (consecutive SOH)"};
        }

        // Split on the first '='.
        std::size_t eq = field.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            return FixError{FixErrorReason::MalformedField, -1,
                            "field missing '=' or empty tag"};
        }

        std::string_view tag_sv = field.substr(0, eq);
        std::string_view val_sv = field.substr(eq + 1);

        int tag = 0;
        auto [ptr, ec] =
            std::from_chars(tag_sv.data(), tag_sv.data() + tag_sv.size(), tag);
        if (ec != std::errc{} || ptr != tag_sv.data() + tag_sv.size()) {
            return FixError{FixErrorReason::MalformedField, -1,
                            "non-numeric tag"};
        }

        map.add(tag, val_sv);
    }

    return map;
}

uint8_t FixMessage::compute_checksum(std::string_view bytes_before_checksum) {
    unsigned sum = 0;
    for (unsigned char c : bytes_before_checksum) {
        sum += c;
    }
    return static_cast<uint8_t>(sum % 256);
}

std::size_t FixMessage::compute_body_length(std::string_view full_message) {
    // BodyLength (tag 9) counts the bytes starting immediately after the
    // SOH that terminates the BodyLength field itself, up to and
    // including the SOH immediately preceding the CheckSum field (tag
    // 10). In practice: find the start of the "35=" field (the byte
    // right after "8=FIX.x.y<SOH>9=NNN<SOH>"), and the start of the
    // "10=" field; the body length is the distance between them.
    std::size_t body_start = full_message.find(std::string(1, kSOH) + "35=");
    if (body_start == std::string_view::npos) return 0;
    body_start += 1;  // move past the SOH to the '3' of "35="

    // Find the checksum field "<SOH>10=".
    std::string needle = std::string(1, kSOH) + "10=";
    std::size_t cksum_pos = full_message.rfind(needle);
    if (cksum_pos == std::string_view::npos) return 0;
    // The body ends at the SOH that precedes "10=" — that SOH is included
    // in the body count, so the body spans [body_start, cksum_pos+1).
    std::size_t body_end = cksum_pos + 1;  // include the SOH before 10=
    if (body_end <= body_start) return 0;
    return body_end - body_start;
}

std::variant<TagMap, FixError> FixMessage::parse_validated(std::string_view raw) {
    // First tokenize structurally.
    auto tokenized = tokenize(raw);
    if (std::holds_alternative<FixError>(tokenized)) {
        return tokenized;
    }
    const TagMap& map = std::get<TagMap>(tokenized);

    // --- CheckSum (tag 10) ---
    // The checksum is computed over all bytes up to and including the SOH
    // right before "10=". Locate that boundary.
    std::string needle = std::string(1, kSOH) + "10=";
    std::size_t cksum_pos = raw.rfind(needle);
    auto cksum_field = map.get(10);
    if (cksum_pos == std::string_view::npos || !cksum_field.has_value()) {
        return FixError{FixErrorReason::MissingRequiredTag, 10,
                        "missing CheckSum (tag 10)"};
    }
    // Bytes before checksum = everything up to and including the SOH that
    // precedes "10=" (i.e. up to cksum_pos + 1).
    std::string_view before = raw.substr(0, cksum_pos + 1);
    uint8_t computed = compute_checksum(before);

    int declared_cksum = -1;
    {
        std::string_view v = *cksum_field;
        auto [ptr, ec] =
            std::from_chars(v.data(), v.data() + v.size(), declared_cksum);
        if (ec != std::errc{}) declared_cksum = -1;
    }
    if (declared_cksum != static_cast<int>(computed)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "declared=%d computed=%u",
                      declared_cksum, static_cast<unsigned>(computed));
        return FixError{FixErrorReason::ChecksumMismatch, 10, buf};
    }

    // --- BodyLength (tag 9) ---
    auto bl_field = map.get(9);
    if (!bl_field.has_value()) {
        return FixError{FixErrorReason::MissingRequiredTag, 9,
                        "missing BodyLength (tag 9)"};
    }
    long declared_bl = -1;
    {
        std::string_view v = *bl_field;
        auto [ptr, ec] =
            std::from_chars(v.data(), v.data() + v.size(), declared_bl);
        if (ec != std::errc{}) declared_bl = -1;
    }
    std::size_t computed_bl = compute_body_length(raw);
    if (declared_bl < 0 || static_cast<std::size_t>(declared_bl) != computed_bl) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "declared=%ld computed=%zu",
                      declared_bl, computed_bl);
        return FixError{FixErrorReason::BodyLengthMismatch, 9, buf};
    }

    return map;
}

}  // namespace miniexchange::fix
