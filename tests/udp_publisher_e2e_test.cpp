#include "adapters/udp/udp_feed_publisher.hpp"
#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace miniexchange;
using namespace miniexchange::udp;

// ---------------------------------------------------------------------------
// Task 14: Publisher end-to-end test against a real MatchingEngine.
//
// Wires UdpFeedPublisher as the real EventSink on a MatchingEngine,
// drives a scripted order sequence that includes:
//   - partial fill at best (non-draining)
//   - full fill of last order at best (draining)
//   - cancel that drains the other side
// Captures all sent wire messages and asserts correctness.
// ---------------------------------------------------------------------------

class PublisherE2ETest : public ::testing::Test {
protected:
    static constexpr SymbolId kSymbol{1};

    struct SentMessage {
        std::vector<std::byte> data;
        MessageType type() const {
            FeedHeader hdr{};
            std::memcpy(&hdr, data.data(), sizeof(hdr));
            return hdr.type;
        }
        uint64_t sequence() const {
            FeedHeader hdr{};
            std::memcpy(&hdr, data.data(), sizeof(hdr));
            return hdr.sequence;
        }
        template <typename T>
        T as() const {
            T msg{};
            std::memcpy(&msg, data.data(), sizeof(T));
            return msg;
        }
    };

    std::vector<SentMessage> sent;

    SendFunction make_capture() {
        return [this](int, const void* data, std::size_t len,
                      const struct sockaddr*, socklen_t) -> ssize_t {
            SentMessage m;
            m.data.resize(len);
            std::memcpy(m.data.data(), data, len);
            sent.push_back(std::move(m));
            return static_cast<ssize_t>(len);
        };
    }

    std::unique_ptr<UdpFeedPublisher> make_publisher(uint64_t snap_interval = 1000) {
        std::vector<Subscriber> subs;
        Subscriber s{};
        std::memset(&s.addr, 0, sizeof(s.addr));
        subs.push_back(s);
        return std::make_unique<UdpFeedPublisher>(
            kSymbol, std::move(subs), -1, snap_interval, make_capture());
    }

    // Helpers to find messages by type
    std::vector<SentMessage> filter(MessageType t) const {
        std::vector<SentMessage> out;
        for (const auto& m : sent) {
            if (m.type() == t) out.push_back(m);
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// Full pipeline: engine → publisher → captured wire messages
// ---------------------------------------------------------------------------

TEST_F(PublisherE2ETest, FullScriptedSequence) {
    auto pub = make_publisher();
    MatchingEngine engine(pub.get());

    // 1. Place two sells at 101 (qty 10 each)
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{101}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{10}}});

    // 2. Place a buy at 100 (rests, establishes best bid)
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{100}, Quantity{20}}});

    // Verify top-of-book at this point
    auto tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{100});
    EXPECT_EQ(tob.bid_qty, Quantity{20});
    EXPECT_EQ(tob.ask_price, Price{101});
    EXPECT_EQ(tob.ask_qty, Quantity{20});  // 10 + 10

    sent.clear();

    // 3. Partial fill: aggressive buy at 101 for qty 5.
    //    Fills 5 from order #1 (partial — resting_order_removed=false).
    //    Ask should show: price=101, qty=15.
    engine.submit(NewOrder{LimitOrder{OrderId{4}, Side::Buy, Price{101}, Quantity{5}}});

    // Check we got a TradeMessage
    auto trades = filter(MessageType::Trade);
    ASSERT_EQ(trades.size(), 1u);
    auto tmsg = trades[0].as<TradeMessage>();
    EXPECT_EQ(tmsg.price, Price{101});
    EXPECT_EQ(tmsg.quantity, Quantity{5});

    tob = pub->current_top_of_book();
    EXPECT_EQ(tob.ask_price, Price{101});
    EXPECT_EQ(tob.ask_qty, Quantity{15});  // 20 - 5

    sent.clear();

    // 4. Full fill that drains: aggressive buy at 101 for qty 15.
    //    Fills remaining 5 from order #1 (removed) + all 10 from order #2 (removed).
    //    Ask should drain to zero.
    engine.submit(NewOrder{LimitOrder{OrderId{5}, Side::Buy, Price{101}, Quantity{15}}});

    trades = filter(MessageType::Trade);
    EXPECT_EQ(trades.size(), 2u);  // two fills (one per resting order)

    tob = pub->current_top_of_book();
    EXPECT_EQ(tob.ask_price, Price{0});   // drained
    EXPECT_EQ(tob.ask_qty, Quantity{0});

    // Verify a TopOfBookMessage with zeroed ask was published
    auto tobs = filter(MessageType::TopOfBook);
    bool found_zeroed_ask = false;
    for (const auto& m : tobs) {
        auto msg = m.as<TopOfBookMessage>();
        if (msg.ask_price == Price{0} && msg.ask_qty == Quantity{0}) {
            found_zeroed_ask = true;
            break;
        }
    }
    EXPECT_TRUE(found_zeroed_ask);

    sent.clear();

    // 5. Cancel that drains the bid side: cancel order #3 (only bid order).
    engine.cancel(OrderId{3});

    tob = pub->current_top_of_book();
    EXPECT_EQ(tob.bid_price, Price{0});   // drained
    EXPECT_EQ(tob.bid_qty, Quantity{0});

    // Verify a TopOfBookMessage with zeroed bid was published
    tobs = filter(MessageType::TopOfBook);
    bool found_zeroed_bid = false;
    for (const auto& m : tobs) {
        auto msg = m.as<TopOfBookMessage>();
        if (msg.bid_price == Price{0} && msg.bid_qty == Quantity{0}) {
            found_zeroed_bid = true;
            break;
        }
    }
    EXPECT_TRUE(found_zeroed_bid);
}

TEST_F(PublisherE2ETest, SequenceIsMonotonicAcrossAllMessages) {
    auto pub = make_publisher();
    MatchingEngine engine(pub.get());

    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{5}}});
    engine.cancel(OrderId{1});

    // All messages should have strictly increasing sequences
    ASSERT_GT(sent.size(), 2u);
    for (std::size_t i = 1; i < sent.size(); ++i) {
        EXPECT_GT(sent[i].sequence(), sent[i - 1].sequence())
            << "Sequence not monotonic at index " << i;
    }
}

TEST_F(PublisherE2ETest, SnapshotReflectsEngineState) {
    // Snapshot every 5 messages
    auto pub = make_publisher(5);
    MatchingEngine engine(pub.get());

    // Generate enough messages to trigger a snapshot
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{99}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{7}}});
    engine.submit(NewOrder{LimitOrder{OrderId{4}, Side::Sell, Price{101}, Quantity{3}}});
    engine.submit(NewOrder{LimitOrder{OrderId{5}, Side::Buy, Price{99}, Quantity{2}}});

    auto snaps = filter(MessageType::Snapshot);
    ASSERT_GE(snaps.size(), 1u);

    auto snap = snaps.back().as<SnapshotMessage>();
    EXPECT_EQ(snap.symbol, kSymbol);
    EXPECT_EQ(snap.bid_price, Price{99});
    EXPECT_EQ(snap.bid_qty, Quantity{19});  // 10+7+2
    EXPECT_EQ(snap.ask_price, Price{101});
    EXPECT_EQ(snap.ask_qty, Quantity{8});   // 5+3
    EXPECT_GT(snap.as_of_sequence, 0u);
}
