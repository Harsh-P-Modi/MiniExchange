// tests/binary_codec_alloc_test.cpp — Task 8: Zero-heap-allocation
// verification (NFR1). Instruments global operator new to prove binary
// encode/decode allocates nothing.
//
// This test MUST be in its own executable because it overrides global
// operator new/delete — that override is TU-global and would break
// tests that legitimately allocate (e.g. GoogleTest internals in other
// test files).

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

// Thread-local allocation counter. Incremented by our overridden operator
// new. Test code resets before the operation under test and asserts it
// remains zero afterward.
thread_local std::size_t g_alloc_count = 0;

}  // namespace

// Override global operator new/delete to count heap allocations.
void* operator new(std::size_t size) {
    ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    if (p) std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    if (p) std::free(p);
}

namespace miniexchange::binary_protocol {
namespace {

class BinaryCodecAllocTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset after any GoogleTest internal allocations in setup.
        g_alloc_count = 0;
    }
};

// --- LimitOrderAddMsg ---

TEST_F(BinaryCodecAllocTest, EncodeLimitOrderAdd_ZeroAllocs) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{2};
    msg.price = Price{100};
    msg.quantity = Quantity{10};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    g_alloc_count = 0;
    encode(msg, buf);
    EXPECT_EQ(g_alloc_count, 0u) << "encode(LimitOrderAdd) allocated!";
}

TEST_F(BinaryCodecAllocTest, DecodeLimitOrderAdd_ZeroAllocs) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{5};
    msg.order_id = OrderId{10};
    msg.price = Price{200};
    msg.quantity = Quantity{50};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    encode(msg, buf);

    g_alloc_count = 0;
    [[maybe_unused]] auto result =
        decode(std::span<const std::byte>(buf.data(), kLimitOrderAddWireSize));
    EXPECT_EQ(g_alloc_count, 0u) << "decode(LimitOrderAdd) allocated!";
}

// --- MarketOrderAddMsg ---

TEST_F(BinaryCodecAllocTest, EncodeMarketOrderAdd_ZeroAllocs) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{2};
    msg.quantity = Quantity{10};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    g_alloc_count = 0;
    encode(msg, buf);
    EXPECT_EQ(g_alloc_count, 0u) << "encode(MarketOrderAdd) allocated!";
}

TEST_F(BinaryCodecAllocTest, DecodeMarketOrderAdd_ZeroAllocs) {
    MarketOrderAddMsg msg{};
    msg.type = MessageType::MarketOrderAdd;
    std::array<std::byte, kMaxMessageWireSize> buf{};
    encode(msg, buf);

    g_alloc_count = 0;
    [[maybe_unused]] auto result =
        decode(std::span<const std::byte>(buf.data(), kMarketOrderAddWireSize));
    EXPECT_EQ(g_alloc_count, 0u) << "decode(MarketOrderAdd) allocated!";
}

// --- CancelMsg ---

TEST_F(BinaryCodecAllocTest, EncodeCancel_ZeroAllocs) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{2};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    g_alloc_count = 0;
    encode(msg, buf);
    EXPECT_EQ(g_alloc_count, 0u) << "encode(Cancel) allocated!";
}

TEST_F(BinaryCodecAllocTest, DecodeCancel_ZeroAllocs) {
    CancelMsg msg{};
    msg.type = MessageType::Cancel;
    std::array<std::byte, kMaxMessageWireSize> buf{};
    encode(msg, buf);

    g_alloc_count = 0;
    [[maybe_unused]] auto result =
        decode(std::span<const std::byte>(buf.data(), kCancelWireSize));
    EXPECT_EQ(g_alloc_count, 0u) << "decode(Cancel) allocated!";
}

// --- AckMsg ---

TEST_F(BinaryCodecAllocTest, EncodeAck_ZeroAllocs) {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{1};
    msg.remaining_qty = Quantity{10};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    g_alloc_count = 0;
    encode(msg, buf);
    EXPECT_EQ(g_alloc_count, 0u) << "encode(Ack) allocated!";
}

TEST_F(BinaryCodecAllocTest, DecodeAck_ZeroAllocs) {
    AckMsg msg{};
    msg.type = MessageType::Ack;
    std::array<std::byte, kMaxMessageWireSize> buf{};
    encode(msg, buf);

    g_alloc_count = 0;
    [[maybe_unused]] auto result =
        decode(std::span<const std::byte>(buf.data(), kAckWireSize));
    EXPECT_EQ(g_alloc_count, 0u) << "decode(Ack) allocated!";
}

// --- RejectMsg ---

TEST_F(BinaryCodecAllocTest, EncodeReject_ZeroAllocs) {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 1;
    msg.order_id = OrderId{1};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    g_alloc_count = 0;
    encode(msg, buf);
    EXPECT_EQ(g_alloc_count, 0u) << "encode(Reject) allocated!";
}

TEST_F(BinaryCodecAllocTest, DecodeReject_ZeroAllocs) {
    RejectMsg msg{};
    msg.type = MessageType::Reject;
    msg.reason_code = 2;
    msg.order_id = OrderId{5};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    encode(msg, buf);

    g_alloc_count = 0;
    [[maybe_unused]] auto result =
        decode(std::span<const std::byte>(buf.data(), kRejectWireSize));
    EXPECT_EQ(g_alloc_count, 0u) << "decode(Reject) allocated!";
}

// --- TradeNotificationMsg ---

TEST_F(BinaryCodecAllocTest, EncodeTradeNotification_ZeroAllocs) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{1};
    msg.sell_order_id = OrderId{2};
    msg.price = Price{100};
    msg.quantity = Quantity{10};
    msg.trade_sequence = TradeSequence{1};

    std::array<std::byte, kMaxMessageWireSize> buf{};
    g_alloc_count = 0;
    encode(msg, buf);
    EXPECT_EQ(g_alloc_count, 0u) << "encode(TradeNotification) allocated!";
}

TEST_F(BinaryCodecAllocTest, DecodeTradeNotification_ZeroAllocs) {
    TradeNotificationMsg msg{};
    msg.type = MessageType::TradeNotification;
    msg.buy_order_id = OrderId{1};
    msg.sell_order_id = OrderId{2};
    msg.price = Price{100};
    msg.quantity = Quantity{10};
    msg.trade_sequence = TradeSequence{1};
    std::array<std::byte, kMaxMessageWireSize> buf{};
    encode(msg, buf);

    g_alloc_count = 0;
    [[maybe_unused]] auto result =
        decode(std::span<const std::byte>(buf.data(), kTradeNotificationWireSize));
    EXPECT_EQ(g_alloc_count, 0u) << "decode(TradeNotification) allocated!";
}

}  // namespace
}  // namespace miniexchange::binary_protocol
