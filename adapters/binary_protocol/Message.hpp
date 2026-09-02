#ifndef MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_MESSAGE_HPP
#define MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_MESSAGE_HPP

#include <cstdint>
#include <type_traits>
#include <variant>

#include "core/Types.hpp"

namespace miniexchange::binary_protocol {

// MessageType — single-byte discriminator, always the first byte on the
// wire. Values are explicit (not auto-incrementing) to make the wire
// format self-documenting and stable across recompilations.
enum class MessageType : uint8_t {
    LimitOrderAdd    = 1,
    MarketOrderAdd   = 2,
    Cancel           = 3,
    Ack              = 4,
    Reject           = 5,
    TradeNotification = 6,
};

// --- Client -> Server messages ---

// LimitOrderAddMsg — submit a limit order.
// Wire layout (all multi-byte fields in network byte order):
//   [1B type][1B side][8B client_id][8B order_id][8B price][8B quantity]
//   Total: 34 bytes
struct LimitOrderAddMsg {
    MessageType type = MessageType::LimitOrderAdd;
    uint8_t     side;        // 0 = Buy, 1 = Sell (explicit width, not enum class)
    ClientId    client_id;
    OrderId     order_id;
    Price       price;
    Quantity    quantity;

    constexpr bool operator==(const LimitOrderAddMsg& o) const {
        return type == o.type && side == o.side &&
               client_id == o.client_id && order_id == o.order_id &&
               price == o.price && quantity == o.quantity;
    }
    constexpr bool operator!=(const LimitOrderAddMsg& o) const {
        return !(*this == o);
    }
};

// MarketOrderAddMsg — submit a market order (no price field).
// Wire layout:
//   [1B type][1B side][8B client_id][8B order_id][8B quantity]
//   Total: 26 bytes
struct MarketOrderAddMsg {
    MessageType type = MessageType::MarketOrderAdd;
    uint8_t     side;
    ClientId    client_id;
    OrderId     order_id;
    Quantity    quantity;

    constexpr bool operator==(const MarketOrderAddMsg& o) const {
        return type == o.type && side == o.side &&
               client_id == o.client_id && order_id == o.order_id &&
               quantity == o.quantity;
    }
    constexpr bool operator!=(const MarketOrderAddMsg& o) const {
        return !(*this == o);
    }
};

// CancelMsg — cancel a resting order.
// Wire layout:
//   [1B type][1B padding][8B client_id][8B order_id]
//   Total: 18 bytes
struct CancelMsg {
    MessageType type = MessageType::Cancel;
    uint8_t     padding = 0;  // alignment/future use; always 0
    ClientId    client_id;
    OrderId     order_id;

    constexpr bool operator==(const CancelMsg& o) const {
        return type == o.type && client_id == o.client_id &&
               order_id == o.order_id;
    }
    constexpr bool operator!=(const CancelMsg& o) const {
        return !(*this == o);
    }
};

// --- Server -> Client messages ---

// AckMsg — order accepted acknowledgement.
// Wire layout:
//   [1B type][1B padding][8B order_id][8B remaining_qty]
//   Total: 18 bytes
struct AckMsg {
    MessageType type = MessageType::Ack;
    uint8_t     padding = 0;
    OrderId     order_id;
    Quantity    remaining_qty;

    constexpr bool operator==(const AckMsg& o) const {
        return type == o.type && order_id == o.order_id &&
               remaining_qty == o.remaining_qty;
    }
    constexpr bool operator!=(const AckMsg& o) const {
        return !(*this == o);
    }
};

// RejectMsg — order/cancel rejected.
// Wire layout:
//   [1B type][1B reason_code][8B order_id]
//   Total: 10 bytes
//
// reason_code values:
//   1 = DuplicateOrderId
//   2 = UnknownOrderId
//   3 = InvalidQuantity
//   4 = InvalidPrice
//   5 = PoolExhausted
//   6 = ParseError (protocol-level, no engine round-trip) / SelfTradePrevented
//   7 = PriceOutOfBand      (Phase 8)
//   8 = QuantityTooLarge    (Phase 8)
//   9 = TickSizeMisaligned  (Phase 8)
//  10 = InternalError       (Phase 11 R3 — engine dispatch threw)
struct RejectMsg {
    MessageType type = MessageType::Reject;
    uint8_t     reason_code;
    OrderId     order_id;

    constexpr bool operator==(const RejectMsg& o) const {
        return type == o.type && reason_code == o.reason_code &&
               order_id == o.order_id;
    }
    constexpr bool operator!=(const RejectMsg& o) const {
        return !(*this == o);
    }
};

// TradeNotificationMsg — a fill occurred.
// Wire layout:
//   [1B type][1B padding][8B buy_order_id][8B sell_order_id]
//   [8B price][8B quantity][8B trade_sequence]
//   Total: 42 bytes
struct TradeNotificationMsg {
    MessageType   type = MessageType::TradeNotification;
    uint8_t       padding = 0;
    OrderId       buy_order_id;
    OrderId       sell_order_id;
    Price         price;
    Quantity      quantity;
    TradeSequence trade_sequence;

    constexpr bool operator==(const TradeNotificationMsg& o) const {
        return type == o.type &&
               buy_order_id == o.buy_order_id &&
               sell_order_id == o.sell_order_id &&
               price == o.price && quantity == o.quantity &&
               trade_sequence == o.trade_sequence;
    }
    constexpr bool operator!=(const TradeNotificationMsg& o) const {
        return !(*this == o);
    }
};

// Compile-time verification: all message structs must be trivially
// copyable (no hidden vtable, no std::string, no heap-owning members).
// If any of these fire, someone added a non-POD member.
static_assert(std::is_trivially_copyable_v<LimitOrderAddMsg>);
static_assert(std::is_trivially_copyable_v<MarketOrderAddMsg>);
static_assert(std::is_trivially_copyable_v<CancelMsg>);
static_assert(std::is_trivially_copyable_v<AckMsg>);
static_assert(std::is_trivially_copyable_v<RejectMsg>);
static_assert(std::is_trivially_copyable_v<TradeNotificationMsg>);

// AnyMessage — decoded output type. std::variant stores all alternatives
// inline (no heap allocation), which is why it was chosen over a
// polymorphic hierarchy with unique_ptr<Message> (that would violate NFR1).
using AnyMessage = std::variant<
    LimitOrderAddMsg,
    MarketOrderAddMsg,
    CancelMsg,
    AckMsg,
    RejectMsg,
    TradeNotificationMsg>;

}  // namespace miniexchange::binary_protocol

#endif  // MINIEXCHANGE_ADAPTERS_BINARY_PROTOCOL_MESSAGE_HPP
