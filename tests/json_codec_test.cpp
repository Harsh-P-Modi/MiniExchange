// tests/json_codec_test.cpp — Task 9: Round-trip tests for the JSON codec.
// Same boundary values as task 6 but no allocation assertion (JSON is
// expected to allocate — that's the comparison point).

#include "adapters/binary_protocol/JsonCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"

#include <gtest/gtest.h>
#include <climits>
#include <nlohmann/json.hpp>

namespace miniexchange::binary_protocol {
namespace {

// Helper: JSON round-trip via serialize then deserialize.
template <typename Msg>
Msg json_round_trip(const Msg& msg) {
    nlohmann::json j = msg;
    return j.get<Msg>();
}

// --- LimitOrderAddMsg ---

TEST(JsonCodecTest, LimitOrderAddRoundTrip) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{42};
    msg.order_id = OrderId{100};
    msg.price = Price{9950};
    msg.quantity = Quantity{500};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, LimitOrderAddMaxFields) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{UINT64_MAX};
    msg.order_id = OrderId{UINT64_MAX};
    msg.price = Price{INT64_MAX};
    msg.quantity = Quantity{UINT64_MAX};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, LimitOrderAddMinPrice) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{1};
    msg.price = Price{INT64_MIN};
    msg.quantity = Quantity{1};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, LimitOrderAddNegativePrice) {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{42};
    msg.order_id = OrderId{7};
    msg.price = Price{-1};
    msg.quantity = Quantity{100};
    EXPECT_EQ(json_round_trip(msg), msg);
}

// --- MarketOrderAddMsg ---

TEST(JsonCodecTest, MarketOrderAddRoundTrip) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{7};
    msg.order_id = OrderId{200};
    msg.quantity = Quantity{1000};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, MarketOrderAddMaxFields) {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{UINT64_MAX};
    msg.order_id = OrderId{UINT64_MAX};
    msg.quantity = Quantity{UINT64_MAX};
    EXPECT_EQ(json_round_trip(msg), msg);
}

// --- CancelMsg ---

TEST(JsonCodecTest, CancelRoundTrip) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{3};
    msg.order_id = OrderId{555};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, CancelMaxFields) {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{UINT64_MAX};
    msg.order_id = OrderId{UINT64_MAX};
    EXPECT_EQ(json_round_trip(msg), msg);
}

// --- AckMsg ---

TEST(JsonCodecTest, AckRoundTrip) {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{42};
    msg.remaining_qty = Quantity{100};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, AckMaxFields) {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{UINT64_MAX};
    msg.remaining_qty = Quantity{UINT64_MAX};
    EXPECT_EQ(json_round_trip(msg), msg);
}

// --- RejectMsg ---

TEST(JsonCodecTest, RejectRoundTrip) {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 3;
    msg.order_id = OrderId{99};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, RejectMaxReasonCode) {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 255;
    msg.order_id = OrderId{UINT64_MAX};
    EXPECT_EQ(json_round_trip(msg), msg);
}

// --- TradeNotificationMsg ---

TEST(JsonCodecTest, TradeNotificationRoundTrip) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{10};
    msg.sell_order_id = OrderId{20};
    msg.price = Price{5000};
    msg.quantity = Quantity{75};
    msg.trade_sequence = TradeSequence{1};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, TradeNotificationMaxFields) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{UINT64_MAX};
    msg.sell_order_id = OrderId{UINT64_MAX};
    msg.price = Price{INT64_MAX};
    msg.quantity = Quantity{UINT64_MAX};
    msg.trade_sequence = TradeSequence{UINT64_MAX};
    EXPECT_EQ(json_round_trip(msg), msg);
}

TEST(JsonCodecTest, TradeNotificationMinPrice) {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{1};
    msg.sell_order_id = OrderId{2};
    msg.price = Price{INT64_MIN};
    msg.quantity = Quantity{1};
    msg.trade_sequence = TradeSequence{1};
    EXPECT_EQ(json_round_trip(msg), msg);
}

}  // namespace
}  // namespace miniexchange::binary_protocol
