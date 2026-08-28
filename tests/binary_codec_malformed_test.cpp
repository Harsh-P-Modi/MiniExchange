// tests/binary_codec_malformed_test.cpp — Task 7: Verify decode returns
// std::nullopt (not UB) for malformed inputs.

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstddef>

namespace miniexchange::binary_protocol {
namespace {

TEST(BinaryCodecMalformedTest, EmptyBuffer) {
    std::span<const std::byte> empty;
    EXPECT_EQ(decode(empty), std::nullopt);
}

TEST(BinaryCodecMalformedTest, ZeroLengthSpan) {
    std::array<std::byte, 1> buf{};
    auto result = decode(std::span<const std::byte>(buf.data(), 0));
    EXPECT_EQ(result, std::nullopt);
}

TEST(BinaryCodecMalformedTest, UnrecognizedTypeByte_Zero) {
    std::array<std::byte, 42> buf{};
    buf[0] = static_cast<std::byte>(0x00);
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, UnrecognizedTypeByte_Seven) {
    std::array<std::byte, 42> buf{};
    buf[0] = static_cast<std::byte>(0x07);
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, UnrecognizedTypeByte_0xFF) {
    std::array<std::byte, 42> buf{};
    buf[0] = static_cast<std::byte>(0xFF);
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TypeByteOnly_LimitOrder) {
    std::array<std::byte, 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::LimitOrderAdd));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TypeByteOnly_MarketOrder) {
    std::array<std::byte, 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::MarketOrderAdd));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TypeByteOnly_Cancel) {
    std::array<std::byte, 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::Cancel));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TypeByteOnly_Ack) {
    std::array<std::byte, 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::Ack));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TypeByteOnly_Reject) {
    std::array<std::byte, 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::Reject));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TypeByteOnly_TradeNotification) {
    std::array<std::byte, 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::TradeNotification));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TruncatedLimitOrder) {
    std::array<std::byte, 5> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::LimitOrderAdd));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TruncatedMarketOrder) {
    std::array<std::byte, 10> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::MarketOrderAdd));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TruncatedCancel) {
    std::array<std::byte, 8> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::Cancel));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TruncatedAck) {
    std::array<std::byte, 9> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::Ack));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TruncatedReject) {
    std::array<std::byte, 5> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::Reject));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, TruncatedTradeNotification) {
    std::array<std::byte, 20> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::TradeNotification));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, OneByteShort_LimitOrder) {
    std::array<std::byte, kLimitOrderAddWireSize - 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::LimitOrderAdd));
    EXPECT_EQ(decode(buf), std::nullopt);
}

TEST(BinaryCodecMalformedTest, OneByteShort_TradeNotification) {
    std::array<std::byte, kTradeNotificationWireSize - 1> buf{};
    buf[0] = static_cast<std::byte>(static_cast<uint8_t>(MessageType::TradeNotification));
    EXPECT_EQ(decode(buf), std::nullopt);
}

// Extra trailing bytes are fine — decode reads only the expected bytes.
TEST(BinaryCodecMalformedTest, ExtraBytesAreIgnored) {
    AckMsg ack;
    ack.type = MessageType::Ack;
    ack.padding = 0;
    ack.order_id = OrderId{42};
    ack.remaining_qty = Quantity{100};

    std::array<std::byte, 64> buf{};
    std::size_t written = encode(ack, buf);
    EXPECT_EQ(written, kAckWireSize);

    // Decode full 64-byte buffer — trailing garbage is ignored.
    auto result = decode(buf);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<AckMsg>(*result), ack);
}

}  // namespace
}  // namespace miniexchange::binary_protocol
