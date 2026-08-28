// tests/binary_codec_test.cpp — Round-trip and correctness tests for
// BinaryCodec encode/decode. Tasks 4-7 share this file.

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstddef>

namespace miniexchange::binary_protocol {
namespace {

// Helper: encode a message into a stack buffer and decode it back.
template <typename Msg>
std::optional<AnyMessage> round_trip(const Msg& msg) {
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    return decode(std::span<const std::byte>(buf.data(), written));
}

// ===== Task 4: LimitOrderAddMsg round-trip =====

TEST(BinaryCodecTest, LimitOrderAddRoundTrip) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;  // Buy
    msg.client_id = ClientId{42};
    msg.order_id = OrderId{100};
    msg.price = Price{9950};
    msg.quantity = Quantity{500};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<LimitOrderAddMsg>(*result));

    auto decoded = std::get<LimitOrderAddMsg>(*result);
    EXPECT_EQ(decoded, msg);
}

TEST(BinaryCodecTest, LimitOrderAddEncodeSize) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;  // Sell
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{2};
    msg.price = Price{100};
    msg.quantity = Quantity{10};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    EXPECT_EQ(written, kLimitOrderAddWireSize);
}

TEST(BinaryCodecTest, LimitOrderAddSellSide) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;  // Sell
    msg.client_id = ClientId{999};
    msg.order_id = OrderId{12345};
    msg.price = Price{-50};  // negative price (valid in the type system)
    msg.quantity = Quantity{1};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    auto decoded = std::get<LimitOrderAddMsg>(*result);
    EXPECT_EQ(decoded, msg);
    EXPECT_EQ(decoded.side, 1);
    EXPECT_EQ(decoded.price, Price{-50});
}

// ===== Task 5: Remaining message types =====

TEST(BinaryCodecTest, MarketOrderAddRoundTrip) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 0;  // Buy
    msg.client_id = ClientId{7};
    msg.order_id = OrderId{200};
    msg.quantity = Quantity{1000};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<MarketOrderAddMsg>(*result));
    EXPECT_EQ(std::get<MarketOrderAddMsg>(*result), msg);
}

TEST(BinaryCodecTest, MarketOrderAddEncodeSize) {
    MarketOrderAddMsg msg{};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    EXPECT_EQ(encode(msg, buf), kMarketOrderAddWireSize);
}

TEST(BinaryCodecTest, CancelRoundTrip) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{3};
    msg.order_id = OrderId{555};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<CancelMsg>(*result));
    EXPECT_EQ(std::get<CancelMsg>(*result), msg);
}

TEST(BinaryCodecTest, CancelEncodeSize) {
    CancelMsg msg{};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    EXPECT_EQ(encode(msg, buf), kCancelWireSize);
}

TEST(BinaryCodecTest, AckRoundTrip) {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{42};
    msg.remaining_qty = Quantity{100};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<AckMsg>(*result));
    EXPECT_EQ(std::get<AckMsg>(*result), msg);
}

TEST(BinaryCodecTest, AckEncodeSize) {
    AckMsg msg{};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    EXPECT_EQ(encode(msg, buf), kAckWireSize);
}

TEST(BinaryCodecTest, RejectRoundTrip) {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 3;  // InvalidQuantity
    msg.order_id = OrderId{99};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<RejectMsg>(*result));
    EXPECT_EQ(std::get<RejectMsg>(*result), msg);
}

TEST(BinaryCodecTest, RejectEncodeSize) {
    RejectMsg msg{};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    EXPECT_EQ(encode(msg, buf), kRejectWireSize);
}

TEST(BinaryCodecTest, TradeNotificationRoundTrip) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{10};
    msg.sell_order_id = OrderId{20};
    msg.price = Price{5000};
    msg.quantity = Quantity{75};
    msg.trade_sequence = TradeSequence{1};

    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<TradeNotificationMsg>(*result));
    EXPECT_EQ(std::get<TradeNotificationMsg>(*result), msg);
}

TEST(BinaryCodecTest, TradeNotificationEncodeSize) {
    TradeNotificationMsg msg{};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    EXPECT_EQ(encode(msg, buf), kTradeNotificationWireSize);
}

// ===== Task 5: Variant-based encode =====

TEST(BinaryCodecTest, VariantEncodeDispatch) {
    AckMsg ack;
    ack.type = MessageType::Ack;
    ack.padding = 0;
    ack.order_id = OrderId{77};
    ack.remaining_qty = Quantity{0};

    AnyMessage any_msg = ack;
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(any_msg, buf);
    EXPECT_EQ(written, kAckWireSize);

    auto result = decode(std::span<const std::byte>(buf.data(), written));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<AckMsg>(*result));
    EXPECT_EQ(std::get<AckMsg>(*result), ack);
}

}  // namespace
}  // namespace miniexchange::binary_protocol
