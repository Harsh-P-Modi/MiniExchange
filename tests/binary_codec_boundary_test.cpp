// tests/binary_codec_boundary_test.cpp — Task 6: Boundary-value round-trip
// property tests (R2 verification) for all six message types. Exercises
// zero, max, min, and negative field values to prove lossless encoding.

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"

#include <gtest/gtest.h>
#include <array>
#include <climits>
#include <cstddef>

namespace miniexchange::binary_protocol {
namespace {

template <typename Msg>
std::optional<AnyMessage> round_trip(const Msg& msg) {
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    return decode(std::span<const std::byte>(buf.data(), written));
}

// --- LimitOrderAddMsg ---

TEST(BinaryCodecBoundaryTest, LimitOrderAddZeroFields) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{0};
    msg.order_id = OrderId{0};
    msg.price = Price{0};
    msg.quantity = Quantity{0};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<LimitOrderAddMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, LimitOrderAddMaxFields) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{UINT64_MAX};
    msg.order_id = OrderId{UINT64_MAX};
    msg.price = Price{INT64_MAX};
    msg.quantity = Quantity{UINT64_MAX};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<LimitOrderAddMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, LimitOrderAddMinPrice) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{1};
    msg.price = Price{INT64_MIN};
    msg.quantity = Quantity{1};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<LimitOrderAddMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, LimitOrderAddNegativePrice) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{42};
    msg.order_id = OrderId{7};
    msg.price = Price{-1};
    msg.quantity = Quantity{100};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<LimitOrderAddMsg>(*result), msg);
}

// --- MarketOrderAddMsg ---

TEST(BinaryCodecBoundaryTest, MarketOrderAddZeroFields) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{0};
    msg.order_id = OrderId{0};
    msg.quantity = Quantity{0};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<MarketOrderAddMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, MarketOrderAddMaxFields) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{UINT64_MAX};
    msg.order_id = OrderId{UINT64_MAX};
    msg.quantity = Quantity{UINT64_MAX};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<MarketOrderAddMsg>(*result), msg);
}

// --- CancelMsg ---

TEST(BinaryCodecBoundaryTest, CancelZeroFields) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{0};
    msg.order_id = OrderId{0};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<CancelMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, CancelMaxFields) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{UINT64_MAX};
    msg.order_id = OrderId{UINT64_MAX};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<CancelMsg>(*result), msg);
}

// --- AckMsg ---

TEST(BinaryCodecBoundaryTest, AckZeroFields) {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{0};
    msg.remaining_qty = Quantity{0};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<AckMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, AckMaxFields) {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{UINT64_MAX};
    msg.remaining_qty = Quantity{UINT64_MAX};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<AckMsg>(*result), msg);
}

// --- RejectMsg ---

TEST(BinaryCodecBoundaryTest, RejectZeroFields) {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 0;
    msg.order_id = OrderId{0};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<RejectMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, RejectMaxReasonCode) {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 255;
    msg.order_id = OrderId{UINT64_MAX};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<RejectMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, RejectAllDefinedReasonCodes) {
    for (uint8_t rc = 1; rc <= 6; ++rc) {
        RejectMsg msg;
        msg.type = MessageType::Reject;
        msg.reason_code = rc;
        msg.order_id = OrderId{rc * 100ULL};
        auto result = round_trip(msg);
        ASSERT_TRUE(result.has_value()) << "reason_code=" << static_cast<int>(rc);
        EXPECT_EQ(std::get<RejectMsg>(*result), msg)
            << "reason_code=" << static_cast<int>(rc);
    }
}

// --- TradeNotificationMsg ---

TEST(BinaryCodecBoundaryTest, TradeNotificationZeroFields) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{0};
    msg.sell_order_id = OrderId{0};
    msg.price = Price{0};
    msg.quantity = Quantity{0};
    msg.trade_sequence = TradeSequence{0};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<TradeNotificationMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, TradeNotificationMaxFields) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{UINT64_MAX};
    msg.sell_order_id = OrderId{UINT64_MAX};
    msg.price = Price{INT64_MAX};
    msg.quantity = Quantity{UINT64_MAX};
    msg.trade_sequence = TradeSequence{UINT64_MAX};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<TradeNotificationMsg>(*result), msg);
}

TEST(BinaryCodecBoundaryTest, TradeNotificationMinPrice) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{1};
    msg.sell_order_id = OrderId{2};
    msg.price = Price{INT64_MIN};
    msg.quantity = Quantity{1};
    msg.trade_sequence = TradeSequence{1};
    auto result = round_trip(msg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<TradeNotificationMsg>(*result), msg);
}

}  // namespace
}  // namespace miniexchange::binary_protocol
