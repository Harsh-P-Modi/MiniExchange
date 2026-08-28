#include "adapters/binary_protocol/BinaryCodec.hpp"

namespace miniexchange::binary_protocol {

using detail::write_field;
using detail::read_field;

// --- Encode: LimitOrderAddMsg ---
// Wire layout: [1B type][1B side][8B client_id][8B order_id][8B price][8B quantity]

std::size_t encode(const LimitOrderAddMsg& msg, std::span<std::byte> out) {
    std::size_t offset = 0;

    auto type_byte = static_cast<uint8_t>(msg.type);
    write_field(out, offset, type_byte);
    write_field(out, offset, msg.side);

    auto client_id = to_network(msg.client_id);
    write_field(out, offset, client_id.value);

    auto order_id = to_network(msg.order_id);
    write_field(out, offset, order_id.value);

    auto price = to_network(msg.price);
    write_field(out, offset, price.value);

    auto quantity = to_network(msg.quantity);
    write_field(out, offset, quantity.value);

    return offset;  // == kLimitOrderAddWireSize (34)
}

// --- Encode: MarketOrderAddMsg ---
// Wire layout: [1B type][1B side][8B client_id][8B order_id][8B quantity]

std::size_t encode(const MarketOrderAddMsg& msg, std::span<std::byte> out) {
    std::size_t offset = 0;

    auto type_byte = static_cast<uint8_t>(msg.type);
    write_field(out, offset, type_byte);
    write_field(out, offset, msg.side);

    auto client_id = to_network(msg.client_id);
    write_field(out, offset, client_id.value);

    auto order_id = to_network(msg.order_id);
    write_field(out, offset, order_id.value);

    auto quantity = to_network(msg.quantity);
    write_field(out, offset, quantity.value);

    return offset;  // == kMarketOrderAddWireSize (26)
}

// --- Encode: CancelMsg ---
// Wire layout: [1B type][1B padding][8B client_id][8B order_id]

std::size_t encode(const CancelMsg& msg, std::span<std::byte> out) {
    std::size_t offset = 0;

    auto type_byte = static_cast<uint8_t>(msg.type);
    write_field(out, offset, type_byte);
    write_field(out, offset, msg.padding);

    auto client_id = to_network(msg.client_id);
    write_field(out, offset, client_id.value);

    auto order_id = to_network(msg.order_id);
    write_field(out, offset, order_id.value);

    return offset;  // == kCancelWireSize (18)
}

// --- Encode: AckMsg ---
// Wire layout: [1B type][1B padding][8B order_id][8B remaining_qty]

std::size_t encode(const AckMsg& msg, std::span<std::byte> out) {
    std::size_t offset = 0;

    auto type_byte = static_cast<uint8_t>(msg.type);
    write_field(out, offset, type_byte);
    write_field(out, offset, msg.padding);

    auto order_id = to_network(msg.order_id);
    write_field(out, offset, order_id.value);

    auto remaining_qty = to_network(msg.remaining_qty);
    write_field(out, offset, remaining_qty.value);

    return offset;  // == kAckWireSize (18)
}

// --- Encode: RejectMsg ---
// Wire layout: [1B type][1B reason_code][8B order_id]

std::size_t encode(const RejectMsg& msg, std::span<std::byte> out) {
    std::size_t offset = 0;

    auto type_byte = static_cast<uint8_t>(msg.type);
    write_field(out, offset, type_byte);
    write_field(out, offset, msg.reason_code);

    auto order_id = to_network(msg.order_id);
    write_field(out, offset, order_id.value);

    return offset;  // == kRejectWireSize (10)
}

// --- Encode: TradeNotificationMsg ---
// Wire layout: [1B type][1B padding][8B buy_order_id][8B sell_order_id]
//              [8B price][8B quantity][8B trade_sequence]

std::size_t encode(const TradeNotificationMsg& msg, std::span<std::byte> out) {
    std::size_t offset = 0;

    auto type_byte = static_cast<uint8_t>(msg.type);
    write_field(out, offset, type_byte);
    write_field(out, offset, msg.padding);

    auto buy_id = to_network(msg.buy_order_id);
    write_field(out, offset, buy_id.value);

    auto sell_id = to_network(msg.sell_order_id);
    write_field(out, offset, sell_id.value);

    auto price = to_network(msg.price);
    write_field(out, offset, price.value);

    auto quantity = to_network(msg.quantity);
    write_field(out, offset, quantity.value);

    auto trade_seq = to_network(msg.trade_sequence);
    write_field(out, offset, trade_seq.value);

    return offset;  // == kTradeNotificationWireSize (42)
}

// --- Variant-based encode ---

std::size_t encode(const AnyMessage& msg, std::span<std::byte> out) {
    return std::visit(
        [&out](const auto& m) { return encode(m, out); },
        msg);
}

// --- Decode ---

std::optional<AnyMessage> decode(std::span<const std::byte> in) {
    if (in.empty()) {
        return std::nullopt;
    }

    auto type_byte = static_cast<uint8_t>(in[0]);
    auto type = static_cast<MessageType>(type_byte);

    switch (type) {
        case MessageType::LimitOrderAdd: {
            if (in.size() < kLimitOrderAddWireSize) return std::nullopt;

            std::size_t offset = 0;
            read_field<uint8_t>(in, offset);  // type (already read)

            LimitOrderAddMsg msg;
            msg.type = type;
            msg.side = read_field<uint8_t>(in, offset);

            uint64_t client_id_val = read_field<uint64_t>(in, offset);
            msg.client_id = from_network(ClientId{client_id_val});

            uint64_t order_id_val = read_field<uint64_t>(in, offset);
            msg.order_id = from_network(OrderId{order_id_val});

            int64_t price_val = read_field<int64_t>(in, offset);
            msg.price = from_network(Price{price_val});

            uint64_t qty_val = read_field<uint64_t>(in, offset);
            msg.quantity = from_network(Quantity{qty_val});

            return AnyMessage{msg};
        }

        case MessageType::MarketOrderAdd: {
            if (in.size() < kMarketOrderAddWireSize) return std::nullopt;

            std::size_t offset = 0;
            read_field<uint8_t>(in, offset);  // type

            MarketOrderAddMsg msg;
            msg.type = type;
            msg.side = read_field<uint8_t>(in, offset);

            uint64_t client_id_val = read_field<uint64_t>(in, offset);
            msg.client_id = from_network(ClientId{client_id_val});

            uint64_t order_id_val = read_field<uint64_t>(in, offset);
            msg.order_id = from_network(OrderId{order_id_val});

            uint64_t qty_val = read_field<uint64_t>(in, offset);
            msg.quantity = from_network(Quantity{qty_val});

            return AnyMessage{msg};
        }

        case MessageType::Cancel: {
            if (in.size() < kCancelWireSize) return std::nullopt;

            std::size_t offset = 0;
            read_field<uint8_t>(in, offset);  // type

            CancelMsg msg;
            msg.type = type;
            msg.padding = read_field<uint8_t>(in, offset);

            uint64_t client_id_val = read_field<uint64_t>(in, offset);
            msg.client_id = from_network(ClientId{client_id_val});

            uint64_t order_id_val = read_field<uint64_t>(in, offset);
            msg.order_id = from_network(OrderId{order_id_val});

            return AnyMessage{msg};
        }

        case MessageType::Ack: {
            if (in.size() < kAckWireSize) return std::nullopt;

            std::size_t offset = 0;
            read_field<uint8_t>(in, offset);  // type

            AckMsg msg;
            msg.type = type;
            msg.padding = read_field<uint8_t>(in, offset);

            uint64_t order_id_val = read_field<uint64_t>(in, offset);
            msg.order_id = from_network(OrderId{order_id_val});

            uint64_t qty_val = read_field<uint64_t>(in, offset);
            msg.remaining_qty = from_network(Quantity{qty_val});

            return AnyMessage{msg};
        }

        case MessageType::Reject: {
            if (in.size() < kRejectWireSize) return std::nullopt;

            std::size_t offset = 0;
            read_field<uint8_t>(in, offset);  // type

            RejectMsg msg;
            msg.type = type;
            msg.reason_code = read_field<uint8_t>(in, offset);

            uint64_t order_id_val = read_field<uint64_t>(in, offset);
            msg.order_id = from_network(OrderId{order_id_val});

            return AnyMessage{msg};
        }

        case MessageType::TradeNotification: {
            if (in.size() < kTradeNotificationWireSize) return std::nullopt;

            std::size_t offset = 0;
            read_field<uint8_t>(in, offset);  // type

            TradeNotificationMsg msg;
            msg.type = type;
            msg.padding = read_field<uint8_t>(in, offset);

            uint64_t buy_id_val = read_field<uint64_t>(in, offset);
            msg.buy_order_id = from_network(OrderId{buy_id_val});

            uint64_t sell_id_val = read_field<uint64_t>(in, offset);
            msg.sell_order_id = from_network(OrderId{sell_id_val});

            int64_t price_val = read_field<int64_t>(in, offset);
            msg.price = from_network(Price{price_val});

            uint64_t qty_val = read_field<uint64_t>(in, offset);
            msg.quantity = from_network(Quantity{qty_val});

            uint64_t trade_seq_val = read_field<uint64_t>(in, offset);
            msg.trade_sequence = from_network(TradeSequence{trade_seq_val});

            return AnyMessage{msg};
        }

        default:
            return std::nullopt;
    }
}

}  // namespace miniexchange::binary_protocol
