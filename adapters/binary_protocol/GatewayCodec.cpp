#include "adapters/binary_protocol/GatewayCodec.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"

namespace miniexchange::binary_protocol {

// --- parse_binary ---

text_protocol::ParseResult parse_binary(std::string_view payload) {
    // Cast string_view to span<const std::byte> for the codec.
    auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(payload.data()), payload.size());

    auto decoded = decode(span);
    if (!decoded.has_value()) {
        return text_protocol::ParseError{"binary decode failed: invalid or truncated message"};
    }

    // Map binary message structs → engine command types.
    return std::visit(
        [](const auto& msg) -> text_protocol::ParseResult {
            using T = std::decay_t<decltype(msg)>;

            if constexpr (std::is_same_v<T, LimitOrderAddMsg>) {
                LimitOrder order;
                order.id = msg.order_id;
                order.side = (msg.side == 0) ? Side::Buy : Side::Sell;
                order.price = msg.price;
                order.quantity = msg.quantity;
                return order;
            } else if constexpr (std::is_same_v<T, MarketOrderAddMsg>) {
                MarketOrder order;
                order.id = msg.order_id;
                order.side = (msg.side == 0) ? Side::Buy : Side::Sell;
                order.quantity = msg.quantity;
                return order;
            } else if constexpr (std::is_same_v<T, CancelMsg>) {
                CancelRequest cancel;
                cancel.id = msg.order_id;
                return cancel;
            } else {
                // Server-to-client messages (Ack, Reject, TradeNotification)
                // should never appear in a client-to-server frame.
                return text_protocol::ParseError{
                    "binary decode failed: received server-to-client message type in request"};
            }
        },
        *decoded);
}

// --- render_binary ---

namespace {

// Map EngineResult rejection codes to binary protocol reason_code values.
// See Message.hpp RejectMsg comment for the mapping.
uint8_t engine_result_to_reason_code(EngineResult result) {
    switch (result) {
        case EngineResult::DuplicateOrderId: return 1;
        case EngineResult::UnknownOrderId:   return 2;
        case EngineResult::InvalidQuantity:  return 3;
        case EngineResult::InvalidPrice:     return 4;
        case EngineResult::PoolExhausted:    return 5;
        default:                             return 0;  // shouldn't happen
    }
}

// Encode a single message into a std::string of raw bytes.
template <typename Msg>
std::string encode_to_string(const Msg& msg) {
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    return std::string(reinterpret_cast<const char*>(buf.data()), written);
}

}  // namespace

std::string render_binary(const EngineResponse& response) {
    if (response.status != EngineResult::Accepted) {
        // Rejection — single RejectMsg.
        RejectMsg reject;
        reject.type = MessageType::Reject;
        reject.reason_code = engine_result_to_reason_code(response.status);
        reject.order_id = OrderId{0};  // order_id not available from response alone
        return encode_to_string(reject);
    }

    // Accepted — AckMsg followed by zero or more TradeNotificationMsgs.
    std::string result;

    AckMsg ack;
    ack.type = MessageType::Ack;
    ack.padding = 0;
    ack.order_id = OrderId{0};  // order_id not carried in EngineResponse
    ack.remaining_qty = response.remaining_qty;
    result += encode_to_string(ack);

    for (const auto& trade : response.trades) {
        TradeNotificationMsg tn;
        tn.type = MessageType::TradeNotification;
        tn.padding = 0;
        tn.buy_order_id = trade.buy_order_id;
        tn.sell_order_id = trade.sell_order_id;
        tn.price = trade.price;
        tn.quantity = trade.quantity;
        tn.trade_sequence = trade.trade_sequence;
        result += encode_to_string(tn);
    }

    return result;
}

// --- render_binary_error ---

std::string render_binary_error(const std::string& /*message*/) {
    // Protocol-level parse error: RejectMsg with reason_code=6, order_id=0.
    RejectMsg reject;
    reject.type = MessageType::Reject;
    reject.reason_code = 6;  // parse error
    reject.order_id = OrderId{0};
    return encode_to_string(reject);
}

}  // namespace miniexchange::binary_protocol
