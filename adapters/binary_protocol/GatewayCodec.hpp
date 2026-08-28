#ifndef MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_GATEWAY_CODEC_HPP
#define MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_GATEWAY_CODEC_HPP

// GatewayCodec — free functions that bridge the binary protocol codec
// with the TCP gateway's frame handler / drain handler pattern. These
// have the same signatures as text_protocol::parse / render / render_error,
// enabling the gateway to switch protocols via std::function binding at
// startup (design.md §5) without an abstract class hierarchy.

#include <string>
#include <string_view>

#include "adapters/text_protocol/text_protocol_parser.hpp"  // ParseResult, ParseError
#include "core/Events.hpp"                                   // EngineResponse

namespace miniexchange::binary_protocol {

// Decodes a binary frame payload into an engine command or parse error.
// Returns the same ParseResult variant type as text_protocol::parse so
// the frame handler lambda works identically for both protocols.
//
// Internally: casts string_view to span<const byte>, calls decode(),
// maps LimitOrderAddMsg → LimitOrder, MarketOrderAddMsg → MarketOrder,
// CancelMsg → CancelRequest. Server-to-client message types in a
// client-to-server frame are treated as parse errors.
[[nodiscard]] text_protocol::ParseResult parse_binary(std::string_view payload);

// Encodes an EngineResponse into binary bytes (returned as std::string
// of raw bytes — same return type as text_protocol::render, suitable
// for passing to frame_message()).
//
// Strategy:
//   - If status == Accepted: encode AckMsg (order_id from the first
//     trade's aggressor side or 0 if no trades, remaining_qty from
//     response). Then for each trade: encode TradeNotificationMsg.
//   - If status is a rejection: encode a single RejectMsg with
//     reason_code mapped from EngineResult.
//
// Note: The EngineResponse doesn't carry the original order_id directly.
// For the gateway response path, the order_id must be threaded through
// by the caller. Since the current gateway architecture (main.cpp)
// doesn't pass order_id separately, we accept OrderId{0} as a default
// in the AckMsg when the order_id isn't available from the response
// alone. The E2E test will verify this is acceptable.
[[nodiscard]] std::string render_binary(const EngineResponse& response);

// Encodes a protocol-level parse error as a binary RejectMsg with
// reason_code=6 (parse error) and order_id=0 (unknown — the parse
// failed before extracting an order ID).
[[nodiscard]] std::string render_binary_error(const std::string& message);

}  // namespace miniexchange::binary_protocol

#endif  // MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_GATEWAY_CODEC_HPP
