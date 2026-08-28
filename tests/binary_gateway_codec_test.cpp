// tests/binary_gateway_codec_test.cpp — Task 10: Unit tests for the
// gateway codec free functions (parse_binary, render_binary, render_binary_error).

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/GatewayCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace miniexchange::binary_protocol {
namespace {

// Helper: encode a binary message into a std::string for passing as payload.
template <typename Msg>
std::string encode_payload(const Msg& msg) {
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    return std::string(reinterpret_cast<const char*>(buf.data()), written);
}

// ===== parse_binary tests =====

TEST(BinaryGatewayCodecTest, ParseLimitOrder) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;  // Buy
    msg.client_id = ClientId{5};
    msg.order_id = OrderId{42};
    msg.price = Price{100};
    msg.quantity = Quantity{10};

    std::string payload = encode_payload(msg);
    auto result = parse_binary(payload);

    ASSERT_TRUE(std::holds_alternative<LimitOrder>(result));
    auto& order = std::get<LimitOrder>(result);
    EXPECT_EQ(order.id, OrderId{42});
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.price, Price{100});
    EXPECT_EQ(order.quantity, Quantity{10});
}

TEST(BinaryGatewayCodecTest, ParseLimitOrderSell) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;  // Sell
    msg.client_id = ClientId{3};
    msg.order_id = OrderId{99};
    msg.price = Price{200};
    msg.quantity = Quantity{50};

    std::string payload = encode_payload(msg);
    auto result = parse_binary(payload);

    ASSERT_TRUE(std::holds_alternative<LimitOrder>(result));
    auto& order = std::get<LimitOrder>(result);
    EXPECT_EQ(order.id, OrderId{99});
    EXPECT_EQ(order.side, Side::Sell);
    EXPECT_EQ(order.price, Price{200});
    EXPECT_EQ(order.quantity, Quantity{50});
}

TEST(BinaryGatewayCodecTest, ParseMarketOrder) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 0;  // Buy
    msg.client_id = ClientId{7};
    msg.order_id = OrderId{200};
    msg.quantity = Quantity{1000};

    std::string payload = encode_payload(msg);
    auto result = parse_binary(payload);

    ASSERT_TRUE(std::holds_alternative<MarketOrder>(result));
    auto& order = std::get<MarketOrder>(result);
    EXPECT_EQ(order.id, OrderId{200});
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.quantity, Quantity{1000});
}

TEST(BinaryGatewayCodecTest, ParseCancel) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{555};

    std::string payload = encode_payload(msg);
    auto result = parse_binary(payload);

    ASSERT_TRUE(std::holds_alternative<CancelRequest>(result));
    auto& cancel = std::get<CancelRequest>(result);
    EXPECT_EQ(cancel.id, OrderId{555});
}

TEST(BinaryGatewayCodecTest, ParseInvalidPayload_Empty) {
    auto result = parse_binary("");
    ASSERT_TRUE(std::holds_alternative<text_protocol::ParseError>(result));
}

TEST(BinaryGatewayCodecTest, ParseInvalidPayload_Truncated) {
    std::string payload(3, '\x01');  // type byte + 2 garbage bytes
    auto result = parse_binary(payload);
    ASSERT_TRUE(std::holds_alternative<text_protocol::ParseError>(result));
}

TEST(BinaryGatewayCodecTest, ParseServerMessage_ReturnsError) {
    // An AckMsg should never appear in a client-to-server frame.
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{1};
    msg.remaining_qty = Quantity{0};

    std::string payload = encode_payload(msg);
    auto result = parse_binary(payload);
    ASSERT_TRUE(std::holds_alternative<text_protocol::ParseError>(result));
}

// ===== render_binary tests =====

TEST(BinaryGatewayCodecTest, RenderAcceptedNoFills) {
    EngineResponse response;
    response.status = EngineResult::Accepted;
    response.remaining_qty = Quantity{10};
    // No trades.

    std::string rendered = render_binary(response);

    // Should contain exactly one AckMsg (18 bytes).
    ASSERT_EQ(rendered.size(), kAckWireSize);

    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<AckMsg>(*decoded));
    auto& ack = std::get<AckMsg>(*decoded);
    EXPECT_EQ(ack.remaining_qty, Quantity{10});
}

TEST(BinaryGatewayCodecTest, RenderAcceptedWithOneFill) {
    EngineResponse response;
    response.status = EngineResult::Accepted;
    response.remaining_qty = Quantity{0};

    Trade trade;
    trade.trade_sequence = TradeSequence{1};
    trade.buy_order_id = OrderId{10};
    trade.sell_order_id = OrderId{20};
    trade.price = Price{100};
    trade.quantity = Quantity{5};
    trade.resting_order_removed = true;
    response.trades.push_back(trade);

    std::string rendered = render_binary(response);

    // AckMsg (18) + TradeNotificationMsg (42) = 60 bytes.
    ASSERT_EQ(rendered.size(), kAckWireSize + kTradeNotificationWireSize);

    // Decode the AckMsg (first 18 bytes).
    auto ack_decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), kAckWireSize));
    ASSERT_TRUE(ack_decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<AckMsg>(*ack_decoded));
    EXPECT_EQ(std::get<AckMsg>(*ack_decoded).remaining_qty, Quantity{0});

    // Decode the TradeNotificationMsg (next 42 bytes).
    auto trade_decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data() + kAckWireSize),
        kTradeNotificationWireSize));
    ASSERT_TRUE(trade_decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<TradeNotificationMsg>(*trade_decoded));
    auto& tn = std::get<TradeNotificationMsg>(*trade_decoded);
    EXPECT_EQ(tn.buy_order_id, OrderId{10});
    EXPECT_EQ(tn.sell_order_id, OrderId{20});
    EXPECT_EQ(tn.price, Price{100});
    EXPECT_EQ(tn.quantity, Quantity{5});
    EXPECT_EQ(tn.trade_sequence, TradeSequence{1});
}

TEST(BinaryGatewayCodecTest, RenderRejection_DuplicateOrderId) {
    EngineResponse response;
    response.status = EngineResult::DuplicateOrderId;
    response.remaining_qty = Quantity{0};

    std::string rendered = render_binary(response);

    ASSERT_EQ(rendered.size(), kRejectWireSize);
    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*decoded));
    auto& reject = std::get<RejectMsg>(*decoded);
    EXPECT_EQ(reject.reason_code, 1);  // DuplicateOrderId
}

TEST(BinaryGatewayCodecTest, RenderRejection_UnknownOrderId) {
    EngineResponse response;
    response.status = EngineResult::UnknownOrderId;
    response.remaining_qty = Quantity{0};

    std::string rendered = render_binary(response);
    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*decoded));
    EXPECT_EQ(std::get<RejectMsg>(*decoded).reason_code, 2);
}

TEST(BinaryGatewayCodecTest, RenderRejection_InvalidQuantity) {
    EngineResponse response;
    response.status = EngineResult::InvalidQuantity;
    response.remaining_qty = Quantity{0};

    std::string rendered = render_binary(response);
    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*decoded));
    EXPECT_EQ(std::get<RejectMsg>(*decoded).reason_code, 3);
}

TEST(BinaryGatewayCodecTest, RenderRejection_InvalidPrice) {
    EngineResponse response;
    response.status = EngineResult::InvalidPrice;
    response.remaining_qty = Quantity{0};

    std::string rendered = render_binary(response);
    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*decoded));
    EXPECT_EQ(std::get<RejectMsg>(*decoded).reason_code, 4);
}

TEST(BinaryGatewayCodecTest, RenderRejection_PoolExhausted) {
    EngineResponse response;
    response.status = EngineResult::PoolExhausted;
    response.remaining_qty = Quantity{0};

    std::string rendered = render_binary(response);
    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*decoded));
    EXPECT_EQ(std::get<RejectMsg>(*decoded).reason_code, 5);
}

// ===== render_binary_error tests =====

TEST(BinaryGatewayCodecTest, RenderError) {
    std::string rendered = render_binary_error("something went wrong");

    ASSERT_EQ(rendered.size(), kRejectWireSize);
    auto decoded = decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(rendered.data()), rendered.size()));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*decoded));
    auto& reject = std::get<RejectMsg>(*decoded);
    EXPECT_EQ(reject.reason_code, 6);  // parse error
    EXPECT_EQ(reject.order_id, OrderId{0});
}

}  // namespace
}  // namespace miniexchange::binary_protocol
