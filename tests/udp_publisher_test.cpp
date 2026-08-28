#include "adapters/udp/udp_feed_publisher.hpp"
#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Events.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace miniexchange;
using namespace miniexchange::udp;

// ---------------------------------------------------------------------------
// Test fixture: captures all sent messages via an injected SendFunction.
// ---------------------------------------------------------------------------

class UdpPublisherTest : public ::testing::Test {
protected:
    static constexpr SymbolId kSymbol{1};
    static constexpr uint64_t kSnapshotInterval = 100;  // every 100 messages

    struct SentMessage {
        std::vector<std::byte> data;
        MessageType type() const {
            if (data.size() < sizeof(FeedHeader)) return MessageType{0};
            FeedHeader hdr{};
            std::memcpy(&hdr, data.data(), sizeof(hdr));
            return hdr.type;
        }
        uint64_t sequence() const {
            if (data.size() < sizeof(FeedHeader)) return 0;
            FeedHeader hdr{};
            std::memcpy(&hdr, data.data(), sizeof(hdr));
            return hdr.sequence;
        }
        template<typename T>
        T as() const {
            T msg{};
            std::memcpy(&msg, data.data(), sizeof(T));
            return msg;
        }
    };

    std::vector<SentMessage> sent_messages;
    bool simulate_ewouldblock = false;

    SendFunction make_capture_send() {
        return [this](int /*fd*/, const void* data, std::size_t len,
                      const struct sockaddr* /*addr*/,
                      socklen_t /*addrlen*/) -> ssize_t {
            if (simulate_ewouldblock) {
                return -1;  // Simulate EWOULDBLOCK
            }
            SentMessage msg;
            msg.data.resize(len);
            std::memcpy(msg.data.data(), data, len);
            sent_messages.push_back(std::move(msg));
            return static_cast<ssize_t>(len);
        };
    }

    std::unique_ptr<UdpFeedPublisher> make_publisher(
        uint64_t snapshot_interval = kSnapshotInterval) {
        std::vector<Subscriber> subs;
        Subscriber s{};
        std::memset(&s.addr, 0, sizeof(s.addr));
        s.addr.sin_family = AF_INET;
        s.addr.sin_port = 0;
        s.addr.sin_addr.s_addr = 0;
        subs.push_back(s);

        return std::make_unique<UdpFeedPublisher>(
            kSymbol, std::move(subs), -1, snapshot_interval, make_capture_send());
    }
};

// ---------------------------------------------------------------------------
// Task 7: Skeleton + state tracking
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, InitialTopOfBookIsZeroed) {
    auto pub = make_publisher();
    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{0});
    EXPECT_EQ(tob.bid_qty, Quantity{0});
    EXPECT_EQ(tob.ask_price, Price{0});
    EXPECT_EQ(tob.ask_qty, Quantity{0});
}

// ---------------------------------------------------------------------------
// Task 8: Non-blocking send / drop on EWOULDBLOCK
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, DropOnEwouldblock) {
    auto pub = make_publisher();
    simulate_ewouldblock = true;

    // Accept an order — this would normally publish a TopOfBookMessage
    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});

    // Messages were "sent" but all dropped
    EXPECT_TRUE(sent_messages.empty());
}

// ---------------------------------------------------------------------------
// Task 9: Sequence counter monotonic
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, SequenceIsMonotonic) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Sell, Quantity{5}, Price{101}});

    ASSERT_GE(sent_messages.size(), 2u);
    EXPECT_LT(sent_messages[0].sequence(), sent_messages[1].sequence());

    // All sequences should be strictly increasing
    for (std::size_t i = 1; i < sent_messages.size(); ++i) {
        EXPECT_GT(sent_messages[i].sequence(), sent_messages[i-1].sequence());
    }
}

// ---------------------------------------------------------------------------
// Task 10: on_trade → TradeMessage + top-of-book
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, OnTradeEmitsTradeMessageAndTopOfBook) {
    auto pub = make_publisher();

    // Set up a resting sell at 101
    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Sell, Quantity{20}, Price{101}});
    sent_messages.clear();

    // Trade fills part of the sell
    Trade trade{TradeSequence{1}, OrderId{2}, OrderId{1}, Price{101}, Quantity{5}, false};
    pub->on_trade(trade);

    // Should have: TradeMessage, then TopOfBookMessage (ask qty reduced)
    ASSERT_GE(sent_messages.size(), 2u);
    EXPECT_EQ(sent_messages[0].type(), MessageType::Trade);
    EXPECT_EQ(sent_messages[1].type(), MessageType::TopOfBook);

    auto tmsg = sent_messages[0].as<TradeMessage>();
    EXPECT_EQ(tmsg.price, Price{101});
    EXPECT_EQ(tmsg.quantity, Quantity{5});
    EXPECT_EQ(tmsg.trade_sequence, TradeSequence{1});

    auto tob = sent_messages[1].as<TopOfBookMessage>();
    EXPECT_EQ(tob.ask_price, Price{101});
    EXPECT_EQ(tob.ask_qty, Quantity{15});  // 20 - 5
}

TEST_F(UdpPublisherTest, OnTradeFullFillDrainsSide) {
    auto pub = make_publisher();

    // Single sell resting at 101
    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Sell, Quantity{10}, Price{101}});
    sent_messages.clear();

    // Trade fully consumes the last order at best ask
    Trade trade{TradeSequence{1}, OrderId{2}, OrderId{1}, Price{101}, Quantity{10}, true};
    pub->on_trade(trade);

    // TradeMessage + TopOfBookMessage with zeroed ask side
    ASSERT_GE(sent_messages.size(), 2u);
    auto tob = sent_messages[1].as<TopOfBookMessage>();
    EXPECT_EQ(tob.ask_price, Price{0});
    EXPECT_EQ(tob.ask_qty, Quantity{0});
}

TEST_F(UdpPublisherTest, PartialFillDoesNotDrain) {
    auto pub = make_publisher();

    // Two sells at 101
    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Sell, Quantity{10}, Price{101}});
    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Sell, Quantity{15}, Price{101}});
    sent_messages.clear();

    // Trade fully removes first order (resting_order_removed=true)
    Trade trade{TradeSequence{1}, OrderId{3}, OrderId{1}, Price{101}, Quantity{10}, true};
    pub->on_trade(trade);

    // count was 2, now 1 — should NOT drain (ask still has order #2)
    auto tob_state = pub->current_top_of_book();
    EXPECT_EQ(tob_state.ask_price, Price{101});
    EXPECT_EQ(tob_state.ask_qty, Quantity{15});  // 25-10=15
}

// ---------------------------------------------------------------------------
// Task 11: on_order_accepted → top-of-book updates
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, AcceptBetterPriceReplacesBest) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Buy, Quantity{5}, Price{102}});

    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{102});
    EXPECT_EQ(tob.bid_qty, Quantity{5});
}

TEST_F(UdpPublisherTest, AcceptSamePriceAggregates) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Buy, Quantity{7}, Price{100}});

    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{100});
    EXPECT_EQ(tob.bid_qty, Quantity{17});
}

TEST_F(UdpPublisherTest, AcceptWorsePriceNoChange) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    sent_messages.clear();

    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Buy, Quantity{5}, Price{98}});

    // No message sent for worse price
    EXPECT_TRUE(sent_messages.empty());
    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{100});
}

TEST_F(UdpPublisherTest, MarketOrderAcceptIgnored) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{0}});

    // Market order (Price{0}) should not affect top-of-book
    EXPECT_TRUE(sent_messages.empty());
    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{0});
}

// ---------------------------------------------------------------------------
// Task 12: on_order_cancelled → drain
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, CancelAtBestDecrements) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Buy, Quantity{5}, Price{100}});
    sent_messages.clear();

    pub->on_order_cancelled(OrderCancelled{OrderId{1}, Quantity{10}, Side::Buy, Price{100}});

    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{100});
    EXPECT_EQ(tob.bid_qty, Quantity{5});  // 15 - 10
}

TEST_F(UdpPublisherTest, CancelLastOrderDrainsSide) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    sent_messages.clear();

    pub->on_order_cancelled(OrderCancelled{OrderId{1}, Quantity{10}, Side::Buy, Price{100}});

    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{0});
    EXPECT_EQ(tob.bid_qty, Quantity{0});

    // Should have emitted a TopOfBookMessage with zeroed bid
    ASSERT_GE(sent_messages.size(), 1u);
    auto msg = sent_messages.back().as<TopOfBookMessage>();
    EXPECT_EQ(msg.bid_price, Price{0});
}

TEST_F(UdpPublisherTest, CancelNotAtBestNoChange) {
    auto pub = make_publisher();

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    sent_messages.clear();

    // Cancel at a different price (not the best)
    pub->on_order_cancelled(OrderCancelled{OrderId{2}, Quantity{5}, Side::Buy, Price{98}});

    EXPECT_TRUE(sent_messages.empty());
}

// ---------------------------------------------------------------------------
// Task 13: Snapshot emission at interval
// ---------------------------------------------------------------------------

TEST_F(UdpPublisherTest, SnapshotEmittedAtInterval) {
    // Use interval of 3 for easy testing
    auto pub = make_publisher(3);

    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    // That generates 1 TopOfBookMessage → messages_since_snapshot = 1

    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Sell, Quantity{5}, Price{101}});
    // That generates 1 TopOfBookMessage → messages_since_snapshot = 2

    pub->on_order_accepted(OrderAccepted{OrderId{3}, Side::Buy, Quantity{3}, Price{100}});
    // That generates 1 TopOfBookMessage → messages_since_snapshot = 3 → snapshot!

    // Find the snapshot message
    bool found_snapshot = false;
    for (const auto& m : sent_messages) {
        if (m.type() == MessageType::Snapshot) {
            found_snapshot = true;
            auto snap = m.as<SnapshotMessage>();
            EXPECT_EQ(snap.symbol, kSymbol);
            EXPECT_EQ(snap.bid_price, Price{100});
            EXPECT_EQ(snap.bid_qty, Quantity{13});  // 10 + 3
            EXPECT_EQ(snap.ask_price, Price{101});
            EXPECT_EQ(snap.ask_qty, Quantity{5});
            EXPECT_GT(snap.as_of_sequence, 0u);
            break;
        }
    }
    EXPECT_TRUE(found_snapshot);
}

TEST_F(UdpPublisherTest, SnapshotReflectsDrainedState) {
    // Use interval of 5
    auto pub = make_publisher(5);

    // Place and cancel a bid — drains the side
    pub->on_order_accepted(OrderAccepted{OrderId{1}, Side::Buy, Quantity{10}, Price{100}});
    pub->on_order_cancelled(OrderCancelled{OrderId{1}, Quantity{10}, Side::Buy, Price{100}});
    // 2 TopOfBookMessages so far (accept + cancel), messages_since = 2

    // Emit 3 more events to hit the threshold
    pub->on_order_accepted(OrderAccepted{OrderId{2}, Side::Sell, Quantity{5}, Price{101}});
    pub->on_order_accepted(OrderAccepted{OrderId{3}, Side::Sell, Quantity{3}, Price{101}});
    pub->on_order_accepted(OrderAccepted{OrderId{4}, Side::Sell, Quantity{2}, Price{101}});
    // messages_since = 5 → snapshot!

    // Find snapshot and verify bid is zeroed
    bool found = false;
    for (const auto& m : sent_messages) {
        if (m.type() == MessageType::Snapshot) {
            auto snap = m.as<SnapshotMessage>();
            EXPECT_EQ(snap.bid_price, Price{0});
            EXPECT_EQ(snap.bid_qty, Quantity{0});
            EXPECT_EQ(snap.ask_price, Price{101});
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}
