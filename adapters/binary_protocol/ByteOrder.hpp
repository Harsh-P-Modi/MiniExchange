#ifndef MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_BYTE_ORDER_HPP
#define MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_BYTE_ORDER_HPP

#include <cstdint>
#include <cstring>
#include <endian.h>    // htobe64, be64toh (Linux/glibc — project target)
#include <arpa/inet.h> // htons, htonl, ntohs, ntohl
#include <type_traits>

namespace miniexchange::binary_protocol {

// --- Raw integer byte-order conversions ---
// Each width uses the standard POSIX/glibc function for host ↔ big-endian.

inline uint16_t to_network(uint16_t v) { return htons(v); }
inline uint16_t from_network(uint16_t v) { return ntohs(v); }

inline uint32_t to_network(uint32_t v) { return htonl(v); }
inline uint32_t from_network(uint32_t v) { return ntohl(v); }

inline uint64_t to_network(uint64_t v) { return htobe64(v); }
inline uint64_t from_network(uint64_t v) { return be64toh(v); }

// Signed 64-bit (Price): byte-swap is bit-pattern-agnostic — reinterpret
// as unsigned, swap, reinterpret back. No sign-extension issues because
// we operate on the raw bit pattern, not the numeric value.
inline int64_t to_network(int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    u = htobe64(u);
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

inline int64_t from_network(int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    u = be64toh(u);
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

// Signed 32-bit (not currently used by core/ types, but included for
// completeness — SymbolId is uint32_t, not int32_t).
inline int32_t to_network(int32_t v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    u = htonl(u);
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

inline int32_t from_network(int32_t v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    u = ntohl(u);
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

// --- Generic overload for core/'s strong-typed wrappers ---
//
// Any type T with a public `.value` member whose size is > 1 byte gets
// byte-swapped by extracting, swapping, and rewrapping. Single-byte types
// (MessageType, uint8_t side) are excluded by the sizeof constraint and
// need no swapping at all.
//
// The `decltype(T::value)` approach avoids requiring core/ types to add
// an `underlying_type` typedef — we deduce it directly from the member.
// The explicit constructor `T{...}` works because all core/ wrappers
// have an explicit constexpr constructor from their underlying type.

template <typename T>
    requires (sizeof(decltype(T::value)) > 1) &&
             requires(T t) { { t.value }; } &&
             (!std::is_same_v<T, uint16_t>) &&
             (!std::is_same_v<T, uint32_t>) &&
             (!std::is_same_v<T, uint64_t>) &&
             (!std::is_same_v<T, int64_t>) &&
             (!std::is_same_v<T, int32_t>)
inline T to_network(T wrapped) {
    using underlying = decltype(T::value);
    return T{static_cast<underlying>(
        to_network(wrapped.value))};
}

template <typename T>
    requires (sizeof(decltype(T::value)) > 1) &&
             requires(T t) { { t.value }; } &&
             (!std::is_same_v<T, uint16_t>) &&
             (!std::is_same_v<T, uint32_t>) &&
             (!std::is_same_v<T, uint64_t>) &&
             (!std::is_same_v<T, int64_t>) &&
             (!std::is_same_v<T, int32_t>)
inline T from_network(T wrapped) {
    using underlying = decltype(T::value);
    return T{static_cast<underlying>(
        from_network(wrapped.value))};
}

}  // namespace miniexchange::binary_protocol

#endif  // MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_BYTE_ORDER_HPP
