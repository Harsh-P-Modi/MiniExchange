#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "adapters/tcp/framing.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "adapters/text_protocol/text_protocol_renderer.hpp"
#include "core/TaggedCommand.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

// ============================================================
// Compile-time verification: SpscRingBuffer<TaggedCommand> and
// SpscRingBuffer<TaggedResponse> are instantiable with the
// project's standard capacity. If TaggedCommand or TaggedResponse
// were not default-constructible (std::array<T,N> requires it),
// this file would fail to compile — catching the issue at build
// time rather than requiring a dedicated runtime test.
// ============================================================

using InboundQueue = miniexchange::SpscRingBuffer<miniexchange::TaggedCommand, 4096>;
using OutboundQueue = miniexchange::SpscRingBuffer<miniexchange::TaggedResponse, 65536>;

// Static assertions: the queue types are constructible.
static_assert(std::is_default_constructible_v<miniexchange::TaggedCommand>,
              "TaggedCommand must be default-constructible for SpscRingBuffer");
static_assert(std::is_default_constructible_v<miniexchange::TaggedResponse>,
              "TaggedResponse must be default-constructible for SpscRingBuffer");

// ============================================================
// Tests: frame_message utility
// ============================================================

TEST(FramingTest, EmptyPayload) {
    auto frame = miniexchange::tcp::frame_message("");
    ASSERT_EQ(frame.size(), 4u);
    // Length prefix should be 0x00000000
    EXPECT_EQ(static_cast<uint8_t>(frame[0]), 0u);
    EXPECT_EQ(static_cast<uint8_t>(frame[1]), 0u);
    EXPECT_EQ(static_cast<uint8_t>(frame[2]), 0u);
    EXPECT_EQ(static_cast<uint8_t>(frame[3]), 0u);
}

TEST(FramingTest, SmallPayload) {
    std::string_view payload = "ADD 1 BUY 100 10";
    auto frame = miniexchange::tcp::frame_message(payload);

    ASSERT_EQ(frame.size(), 4 + payload.size());

    // Verify length prefix (big-endian)
    auto len = static_cast<uint32_t>(payload.size());
    EXPECT_EQ(static_cast<uint8_t>(frame[0]),
              static_cast<uint8_t>((len >> 24) & 0xFF));
    EXPECT_EQ(static_cast<uint8_t>(frame[1]),
              static_cast<uint8_t>((len >> 16) & 0xFF));
    EXPECT_EQ(static_cast<uint8_t>(frame[2]),
              static_cast<uint8_t>((len >> 8) & 0xFF));
    EXPECT_EQ(static_cast<uint8_t>(frame[3]),
              static_cast<uint8_t>(len & 0xFF));

    // Verify payload bytes
    EXPECT_EQ(std::string_view(frame.data() + 4, payload.size()), payload);
}

TEST(FramingTest, LengthPrefixIsBigEndian) {
    // Create a payload of exactly 256 bytes to verify byte ordering
    std::string payload(256, 'x');
    auto frame = miniexchange::tcp::frame_message(payload);

    // 256 = 0x00000100 in big-endian
    EXPECT_EQ(static_cast<uint8_t>(frame[0]), 0x00u);
    EXPECT_EQ(static_cast<uint8_t>(frame[1]), 0x00u);
    EXPECT_EQ(static_cast<uint8_t>(frame[2]), 0x01u);
    EXPECT_EQ(static_cast<uint8_t>(frame[3]), 0x00u);
}

TEST(FramingTest, PayloadIntegrity) {
    // Binary-safe: payload can contain null bytes and arbitrary data
    std::string payload = "CANCEL 42";
    payload += '\0';
    payload += "trailing";

    auto frame = miniexchange::tcp::frame_message(payload);
    ASSERT_EQ(frame.size(), 4 + payload.size());

    std::string_view extracted(frame.data() + 4, payload.size());
    EXPECT_EQ(extracted, std::string_view(payload.data(), payload.size()));
}

// ============================================================
// Tests: SpscRingBuffer<TaggedCommand> push/pop round-trip
// ============================================================

TEST(InboundQueueTest, PushPopLimitOrder) {
    InboundQueue queue;

    miniexchange::TaggedCommand cmd;
    cmd.client = miniexchange::ClientId{7};
    cmd.command = miniexchange::LimitOrder{
        miniexchange::OrderId{42}, miniexchange::Side::Buy,
        miniexchange::Price{100}, miniexchange::Quantity{10}};

    ASSERT_TRUE(queue.try_push(std::move(cmd)));

    miniexchange::TaggedCommand popped;
    ASSERT_TRUE(queue.try_pop(popped));

    EXPECT_EQ(popped.client, miniexchange::ClientId{7});
    auto* limit = std::get_if<miniexchange::LimitOrder>(&popped.command);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->id, miniexchange::OrderId{42});
    EXPECT_EQ(limit->side, miniexchange::Side::Buy);
    EXPECT_EQ(limit->price, miniexchange::Price{100});
    EXPECT_EQ(limit->quantity, miniexchange::Quantity{10});
}

TEST(InboundQueueTest, PushPopMarketOrder) {
    InboundQueue queue;

    miniexchange::TaggedCommand cmd;
    cmd.client = miniexchange::ClientId{3};
    cmd.command = miniexchange::MarketOrder{miniexchange::OrderId{99},
                                            miniexchange::Side::Sell,
                                            miniexchange::Quantity{50}};

    ASSERT_TRUE(queue.try_push(std::move(cmd)));

    miniexchange::TaggedCommand popped;
    ASSERT_TRUE(queue.try_pop(popped));

    EXPECT_EQ(popped.client, miniexchange::ClientId{3});
    auto* market = std::get_if<miniexchange::MarketOrder>(&popped.command);
    ASSERT_NE(market, nullptr);
    EXPECT_EQ(market->id, miniexchange::OrderId{99});
    EXPECT_EQ(market->side, miniexchange::Side::Sell);
    EXPECT_EQ(market->quantity, miniexchange::Quantity{50});
}

TEST(InboundQueueTest, PushPopCancelRequest) {
    InboundQueue queue;

    miniexchange::TaggedCommand cmd;
    cmd.client = miniexchange::ClientId{1};
    cmd.command = miniexchange::CancelRequest{miniexchange::OrderId{55}};

    ASSERT_TRUE(queue.try_push(std::move(cmd)));

    miniexchange::TaggedCommand popped;
    ASSERT_TRUE(queue.try_pop(popped));

    EXPECT_EQ(popped.client, miniexchange::ClientId{1});
    auto* cancel = std::get_if<miniexchange::CancelRequest>(&popped.command);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(cancel->id, miniexchange::OrderId{55});
}

// ============================================================
// Tests: SpscRingBuffer<TaggedResponse> push/pop round-trip
// ============================================================

TEST(OutboundQueueTest, PushPopResponse) {
    // Use a small capacity (still power of 2) for test speed
    miniexchange::SpscRingBuffer<miniexchange::TaggedResponse, 16> queue;

    miniexchange::TaggedResponse resp;
    resp.client = miniexchange::ClientId{5};
    resp.response.status = miniexchange::EngineResult::Accepted;
    resp.response.remaining_qty = miniexchange::Quantity{0};
    resp.response.trades.push_back(miniexchange::Trade{
        miniexchange::TradeSequence{1}, miniexchange::OrderId{10},
        miniexchange::OrderId{20}, miniexchange::Price{100},
        miniexchange::Quantity{5}});

    ASSERT_TRUE(queue.try_push(std::move(resp)));

    miniexchange::TaggedResponse popped;
    ASSERT_TRUE(queue.try_pop(popped));

    EXPECT_EQ(popped.client, miniexchange::ClientId{5});
    EXPECT_EQ(popped.response.status, miniexchange::EngineResult::Accepted);
    EXPECT_EQ(popped.response.remaining_qty, miniexchange::Quantity{0});
    ASSERT_EQ(popped.response.trades.size(), 1u);
    EXPECT_EQ(popped.response.trades[0].price, miniexchange::Price{100});
}

// ============================================================
// Tests: Parse → push integration pattern (no TcpServer needed)
// ============================================================

TEST(ParseToPushTest, ValidLimitOrderParsesAndQueues) {
    InboundQueue queue;
    miniexchange::ClientId client{42};

    auto result = miniexchange::text_protocol::parse("ADD 1 BUY 100 10");

    // Should parse to LimitOrder
    auto* limit = std::get_if<miniexchange::LimitOrder>(&result);
    ASSERT_NE(limit, nullptr);

    // Simulate the frame handler's push logic
    miniexchange::TaggedCommand tagged{client, *limit};
    ASSERT_TRUE(queue.try_push(std::move(tagged)));

    miniexchange::TaggedCommand popped;
    ASSERT_TRUE(queue.try_pop(popped));
    EXPECT_EQ(popped.client, client);

    auto* popped_limit = std::get_if<miniexchange::LimitOrder>(&popped.command);
    ASSERT_NE(popped_limit, nullptr);
    EXPECT_EQ(popped_limit->id, miniexchange::OrderId{1});
}

TEST(ParseToPushTest, ValidCancelParsesAndQueues) {
    InboundQueue queue;
    miniexchange::ClientId client{7};

    auto result = miniexchange::text_protocol::parse("CANCEL 99");

    auto* cancel = std::get_if<miniexchange::CancelRequest>(&result);
    ASSERT_NE(cancel, nullptr);

    miniexchange::TaggedCommand tagged{client, *cancel};
    ASSERT_TRUE(queue.try_push(std::move(tagged)));

    miniexchange::TaggedCommand popped;
    ASSERT_TRUE(queue.try_pop(popped));
    EXPECT_EQ(popped.client, client);

    auto* popped_cancel =
        std::get_if<miniexchange::CancelRequest>(&popped.command);
    ASSERT_NE(popped_cancel, nullptr);
    EXPECT_EQ(popped_cancel->id, miniexchange::OrderId{99});
}

TEST(ParseToPushTest, ParseErrorRendersDirectResponse) {
    // When the text protocol parser returns a ParseError, the frame
    // handler should render it directly and send back without queuing.
    auto result = miniexchange::text_protocol::parse("INVALID COMMAND");

    auto* err = std::get_if<miniexchange::text_protocol::ParseError>(&result);
    ASSERT_NE(err, nullptr);

    // Render the error response
    std::string response =
        miniexchange::text_protocol::render_error(err->message);
    EXPECT_FALSE(response.empty());
    EXPECT_NE(response.find("ERROR:"), std::string::npos);

    // Frame it for sending over the wire
    auto framed = miniexchange::tcp::frame_message(response);
    EXPECT_EQ(framed.size(), 4 + response.size());
}

TEST(ParseToPushTest, QueueFullRejectsSilently) {
    // R4 back-pressure: when inbound queue is full, try_push returns
    // false and the frame is dropped (client command lost — acceptable
    // per requirements.md R4).
    miniexchange::SpscRingBuffer<miniexchange::TaggedCommand, 4> queue;

    miniexchange::TaggedCommand cmd;
    cmd.client = miniexchange::ClientId{1};
    cmd.command = miniexchange::LimitOrder{
        miniexchange::OrderId{1}, miniexchange::Side::Buy,
        miniexchange::Price{100}, miniexchange::Quantity{10}};

    // Fill the queue to capacity
    for (int i = 0; i < 4; ++i) {
        cmd.command = miniexchange::LimitOrder{
            miniexchange::OrderId{static_cast<uint64_t>(i)},
            miniexchange::Side::Buy, miniexchange::Price{100},
            miniexchange::Quantity{10}};
        ASSERT_TRUE(queue.try_push(cmd));
    }

    // Next push should fail (back-pressure)
    EXPECT_FALSE(queue.try_push(cmd));
}
