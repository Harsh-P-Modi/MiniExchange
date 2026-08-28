#ifndef MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_BINARY_CODEC_HPP
#define MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_BINARY_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

#include "adapters/binary_protocol/ByteOrder.hpp"
#include "adapters/binary_protocol/Message.hpp"

namespace miniexchange::binary_protocol {

// --- Wire sizes (logical byte count per message, no struct padding) ---
// These are the number of bytes actually written/read on the wire.

inline constexpr std::size_t kLimitOrderAddWireSize    = 34;  // 1+1+8+8+8+8
inline constexpr std::size_t kMarketOrderAddWireSize   = 26;  // 1+1+8+8+8
inline constexpr std::size_t kCancelWireSize           = 18;  // 1+1+8+8
inline constexpr std::size_t kAckWireSize              = 18;  // 1+1+8+8
inline constexpr std::size_t kRejectWireSize           = 10;  // 1+1+8
inline constexpr std::size_t kTradeNotificationWireSize = 42; // 1+1+8+8+8+8+8

// Maximum wire size across all message types (useful for stack buffer sizing).
inline constexpr std::size_t kMaxMessageWireSize = kTradeNotificationWireSize;

// --- Encode ---
// Writes a message into a caller-provided buffer. Returns bytes written.
// Precondition: out.size() >= wire size for the message type.
// No heap allocation — writes directly into the provided span.

std::size_t encode(const LimitOrderAddMsg& msg, std::span<std::byte> out);
std::size_t encode(const MarketOrderAddMsg& msg, std::span<std::byte> out);
std::size_t encode(const CancelMsg& msg, std::span<std::byte> out);
std::size_t encode(const AckMsg& msg, std::span<std::byte> out);
std::size_t encode(const RejectMsg& msg, std::span<std::byte> out);
std::size_t encode(const TradeNotificationMsg& msg, std::span<std::byte> out);

// Variant-based encode: dispatches to the correct overload.
std::size_t encode(const AnyMessage& msg, std::span<std::byte> out);

// --- Decode ---
// Reads the leading MessageType byte to determine which fixed-size
// struct follows, then decodes field-by-field. Returns std::nullopt if:
// - Buffer is empty
// - MessageType byte is unrecognized
// - Buffer is shorter than the expected wire size for the detected type

std::optional<AnyMessage> decode(std::span<const std::byte> in);

// --- Helpers for encode/decode internals ---
namespace detail {

// Write a trivial value at offset, advance offset. No allocation.
template <typename T>
inline void write_field(std::span<std::byte> out, std::size_t& offset, T val) {
    std::memcpy(out.data() + offset, &val, sizeof(T));
    offset += sizeof(T);
}

// Read a trivial value at offset, advance offset.
template <typename T>
inline T read_field(std::span<const std::byte> in, std::size_t& offset) {
    T val;
    std::memcpy(&val, in.data() + offset, sizeof(T));
    offset += sizeof(T);
    return val;
}

}  // namespace detail

}  // namespace miniexchange::binary_protocol

#endif  // MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_BINARY_CODEC_HPP
