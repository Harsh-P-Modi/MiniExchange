#ifndef MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_JSON_CODEC_HPP
#define MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_JSON_CODEC_HPP

// JsonCodec — nlohmann/json-based ADL serialization for the six binary
// protocol message types. Exists solely for the Phase 7 comparison
// benchmark (R3): never a production wire format, never linked into
// core/ or engine/.

#include <nlohmann/json.hpp>

#include "adapters/binary_protocol/Message.hpp"

namespace miniexchange::binary_protocol {

// --- ADL to_json / from_json for each message type ---
// Field names match C++ struct field names for readability in benchmark
// captured payloads, not for wire compatibility.

void to_json(nlohmann::json& j, const LimitOrderAddMsg& msg);
void from_json(const nlohmann::json& j, LimitOrderAddMsg& msg);

void to_json(nlohmann::json& j, const MarketOrderAddMsg& msg);
void from_json(const nlohmann::json& j, MarketOrderAddMsg& msg);

void to_json(nlohmann::json& j, const CancelMsg& msg);
void from_json(const nlohmann::json& j, CancelMsg& msg);

void to_json(nlohmann::json& j, const AckMsg& msg);
void from_json(const nlohmann::json& j, AckMsg& msg);

void to_json(nlohmann::json& j, const RejectMsg& msg);
void from_json(const nlohmann::json& j, RejectMsg& msg);

void to_json(nlohmann::json& j, const TradeNotificationMsg& msg);
void from_json(const nlohmann::json& j, TradeNotificationMsg& msg);

}  // namespace miniexchange::binary_protocol

#endif  // MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_JSON_CODEC_HPP
