#include "adapters/udp/book_builder.hpp"
#include "adapters/udp/udp_feed_publisher.hpp"
#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "orderbook/order_book.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <vector>

using namespace miniexchange;
using namespace miniexchange::udp;

// ---------------------------------------------------------------------------
// Full pipeline test fixture:
//   MatchingEngine + UdpFeedPublisher → captured wire bytes → UdpFeedBookBuilder
//
// The test drives orders through the engine, captures the raw wire messages
// published by UdpFeedPublisher, then feeds them into UdpFeedBookBuilder.
// The BookBuilder's reconstructed state is compared against the engine's
// actual OrderBook (queried directly by the test, never by the BookBuilder).
// ---------------------------------------------------------------------------

class UdpE2EDoDTest : public ::testing::Test {
protected:
    static constexpr SymbolId kSymbol{1};
    // Small snapshot interval so snapshots appear in the test sequence
    static constexpr uint64_t kSnapshotInterval = 10;

    struct RawMessage {
        std::vector<std::byte> data;
    };

    std::vector<RawMessage> wire_traffic;

    SendFunction make_capture() {
        return [this](int, const void* data, std::size_t len,
                      const struct sockaddr*, socklen_t) -> ssize_t {
            RawMessage m;
            m.data.resize(len);
            std::memcpy(m.data.data(), data, len);
            wire_traffic.push_back(std::move(m));
            return static_cast<ssize_t>(len);
        };
    }

    std::unique_ptr<UdpFeedPublisher> make_publisher() {
        std::vector<Subscriber> subs;
        Subscriber s{};
        std::memset(&s.addr, 0, sizeof(s.addr));
        subs.push_back(s);
        return std::make_unique<UdpFeedPublisher>(
            kSymbol, std::move(subs), -1, kSnapshotInterval, make_capture());
    }

    // Feed all captured wire traffic into a BookBuilder
    void replay_into(UdpFeedBookBuilder& bb) {
        for (const auto& msg : wire_traffic) {
            bb.on_message(msg.data.data(), msg.data.size());
        }
    }

    // Feed captured wire traffic into a BookBuilder, dropping messages
    // at the specified indices (simulating packet loss)
    void replay_with_drops(UdpFeedBookBuilder& bb,
                           const std::vector<std::size_t>& drop_indices) {
        for (std::size_t i = 0; i < wire_traffic.size(); ++i) {
            if (std::find(drop_indices.begin(), drop_indices.end(), i) !=
                drop_indices.end()) {
                continue;  // dropped
            }
            bb.on_message(wire_traffic[i].data.data(),
                          wire_traffic[i].data.size());
        }
    }

    // Get the engine's actual top-of-book from OrderBook
    TopOfBook engine_top_of_book(const MatchingEngine& engine) {
        const auto& book = engine.book();
        TopOfBook tob{};

        auto* best_bid = const_cast<OrderBook&>(
            static_cast<const OrderBook&>(book)).best_bid();
        if (best_bid) {
            tob.bid_price = best_bid->price();
            tob.bid_qty = best_bid->total_quantity();
        }

        auto* best_ask = const_cast<OrderBook&>(
            static_cast<const OrderBook&>(book)).best_ask();
        if (best_ask) {
            tob.ask_price = best_ask->price();
            tob.ask_qty = best_ask->total_quantity();
        }

        return tob;
    }
};

// ---------------------------------------------------------------------------
// Task 20: Definition of Done #1 — Reconstruction matches engine state
// ---------------------------------------------------------------------------

TEST_F(UdpE2EDoDTest, ReconstructedBookMatchesEngine) {
    auto pub = make_publisher();
    MatchingEngine engine(pub.get());

    // Script: build up book, partial fills, cancels, drain-and-recover
    // Phase 1: establish resting orders
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{99}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{99}, Quantity{15}}});
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Sell, Price{101}, Quantity{20}}});
    engine.submit(NewOrder{LimitOrder{OrderId{4}, Side::Sell, Price{101}, Quantity{8}}});

    // Phase 2: partial fill (aggressive sell at 99 for 5)
    engine.submit(NewOrder{LimitOrder{OrderId{5}, Side::Sell, Price{99}, Quantity{5}}});

    // Phase 3: cancel one of the sells
    engine.cancel(OrderId{4});

    // Phase 4: drain the ask completely (aggressive buy sweeps all)
    engine.submit(NewOrder{LimitOrder{OrderId{6}, Side::Buy, Price{101}, Quantity{20}}});

    // Phase 5: new orders re-establish both sides
    engine.submit(NewOrder{LimitOrder{OrderId{7}, Side::Buy, Price{98}, Quantity{12}}});
    engine.submit(NewOrder{LimitOrder{OrderId{8}, Side::Sell, Price{102}, Quantity{7}}});

    // Now replay into BookBuilder
    UdpFeedBookBuilder bb;
    replay_into(bb);

    // The builder should be anchored (snapshots were emitted at interval 10)
    ASSERT_TRUE(bb.is_anchored(kSymbol));

    // Compare publisher's view with engine's actual book
    auto pub_tob = pub->current_top_of_book();
    auto engine_tob = engine_top_of_book(engine);
    auto builder_tob = bb.top_of_book(kSymbol);

    ASSERT_TRUE(builder_tob.has_value());

    // Publisher's view should match engine
    EXPECT_EQ(pub_tob.bid_price, engine_tob.bid_price);
    EXPECT_EQ(pub_tob.bid_qty, engine_tob.bid_qty);
    EXPECT_EQ(pub_tob.ask_price, engine_tob.ask_price);
    EXPECT_EQ(pub_tob.ask_qty, engine_tob.ask_qty);

    // BookBuilder's view should match engine
    EXPECT_EQ(builder_tob->bid_price, engine_tob.bid_price);
    EXPECT_EQ(builder_tob->bid_qty, engine_tob.bid_qty);
    EXPECT_EQ(builder_tob->ask_price, engine_tob.ask_price);
    EXPECT_EQ(builder_tob->ask_qty, engine_tob.ask_qty);

    // Should not be stale (no drops)
    EXPECT_FALSE(bb.is_stale(kSymbol));
}

TEST_F(UdpE2EDoDTest, ReconstructionAfterDrainAndRecover) {
    // Use a very small snapshot interval so we're guaranteed a snapshot
    auto pub_local = [this]() {
        std::vector<Subscriber> subs;
        Subscriber s{};
        std::memset(&s.addr, 0, sizeof(s.addr));
        subs.push_back(s);
        return std::make_unique<UdpFeedPublisher>(
            kSymbol, std::move(subs), -1, 3, make_capture());
    }();

    MatchingEngine engine(pub_local.get());

    // Establish asks
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});

    // Establish bid
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{99}, Quantity{20}}});

    // Drain ask: aggressive buy at 100
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{100}, Quantity{10}}});

    // Re-establish ask
    engine.submit(NewOrder{LimitOrder{OrderId{4}, Side::Sell, Price{103}, Quantity{5}}});

    // Add more to trigger more snapshots
    engine.submit(NewOrder{LimitOrder{OrderId{5}, Side::Buy, Price{98}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{6}, Side::Sell, Price{104}, Quantity{1}}});

    UdpFeedBookBuilder bb;
    replay_into(bb);

    ASSERT_TRUE(bb.is_anchored(kSymbol));
    auto builder_tob = bb.top_of_book(kSymbol);
    auto engine_tob = engine_top_of_book(engine);

    ASSERT_TRUE(builder_tob.has_value());
    // After drain+recovery, the ask should reflect the new price
    EXPECT_EQ(builder_tob->ask_price, engine_tob.ask_price);
    EXPECT_EQ(builder_tob->ask_qty, engine_tob.ask_qty);
    EXPECT_FALSE(bb.is_stale(kSymbol));
}

// ---------------------------------------------------------------------------
// Task 21: Definition of Done #2 — Gap detection under packet loss
// ---------------------------------------------------------------------------

TEST_F(UdpE2EDoDTest, GapDetectionUnderPacketLoss) {
    auto pub = make_publisher();
    MatchingEngine engine(pub.get());

    // Generate enough traffic to have a snapshot + incrementals
    engine.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{99}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{2}, Side::Sell, Price{101}, Quantity{10}}});
    engine.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy, Price{99}, Quantity{5}}});
    engine.submit(NewOrder{LimitOrder{OrderId{4}, Side::Sell, Price{101}, Quantity{3}}});
    engine.submit(NewOrder{LimitOrder{OrderId{5}, Side::Buy, Price{99}, Quantity{2}}});
    engine.submit(NewOrder{LimitOrder{OrderId{6}, Side::Sell, Price{101}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{7}, Side::Buy, Price{99}, Quantity{4}}});
    engine.submit(NewOrder{LimitOrder{OrderId{8}, Side::Sell, Price{101}, Quantity{6}}});
    engine.submit(NewOrder{LimitOrder{OrderId{9}, Side::Buy, Price{99}, Quantity{3}}});
    engine.submit(NewOrder{LimitOrder{OrderId{10}, Side::Sell, Price{101}, Quantity{2}}});
    // Should have triggered a snapshot by now (interval=10 messages)

    // More orders after the snapshot to create incrementals
    engine.submit(NewOrder{LimitOrder{OrderId{11}, Side::Buy, Price{99}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{12}, Side::Sell, Price{101}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{13}, Side::Buy, Price{99}, Quantity{1}}});

    // Find the snapshot index — we need to keep it but drop something after
    std::size_t snapshot_idx = 0;
    for (std::size_t i = 0; i < wire_traffic.size(); ++i) {
        FeedHeader hdr{};
        std::memcpy(&hdr, wire_traffic[i].data.data(), sizeof(hdr));
        if (hdr.type == MessageType::Snapshot) {
            snapshot_idx = i;
        }
    }

    // Drop 2 messages after the last snapshot (simulating packet loss)
    std::vector<std::size_t> drops;
    if (snapshot_idx + 2 < wire_traffic.size()) {
        drops.push_back(snapshot_idx + 1);
        drops.push_back(snapshot_idx + 2);
    }

    int gap_count = 0;
    UdpFeedBookBuilder bb([&gap_count](SymbolId, uint64_t, uint64_t) {
        gap_count++;
    });

    replay_with_drops(bb, drops);

    // BookBuilder should detect the gap and flag as stale
    EXPECT_TRUE(bb.is_stale(kSymbol));
    EXPECT_GT(gap_count, 0);

    // Now feed a fresh snapshot — should clear staleness
    // Generate more traffic to trigger another snapshot
    engine.submit(NewOrder{LimitOrder{OrderId{14}, Side::Buy, Price{99}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{15}, Side::Sell, Price{101}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{16}, Side::Buy, Price{99}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{17}, Side::Sell, Price{101}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{18}, Side::Buy, Price{99}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{19}, Side::Sell, Price{101}, Quantity{1}}});
    engine.submit(NewOrder{LimitOrder{OrderId{20}, Side::Buy, Price{99}, Quantity{1}}});

    // Find the new snapshot in the new traffic
    bool found_new_snapshot = false;
    for (std::size_t i = wire_traffic.size() - 20; i < wire_traffic.size(); ++i) {
        FeedHeader hdr{};
        if (wire_traffic[i].data.size() >= sizeof(hdr)) {
            std::memcpy(&hdr, wire_traffic[i].data.data(), sizeof(hdr));
            if (hdr.type == MessageType::Snapshot) {
                // Feed just this snapshot to the stale builder
                bb.on_message(wire_traffic[i].data.data(),
                              wire_traffic[i].data.size());
                found_new_snapshot = true;
                break;
            }
        }
    }

    if (found_new_snapshot) {
        // Staleness should be cleared by the snapshot
        EXPECT_FALSE(bb.is_stale(kSymbol));
    }
}

TEST_F(UdpE2EDoDTest, NoDropsNoStaleness) {
    auto pub = make_publisher();
    MatchingEngine engine(pub.get());

    // Generate enough traffic with a snapshot
    for (uint64_t i = 1; i <= 15; ++i) {
        Side side = (i % 2 == 0) ? Side::Sell : Side::Buy;
        Price price = (side == Side::Buy) ? Price{99} : Price{101};
        engine.submit(NewOrder{LimitOrder{OrderId{i}, side, price, Quantity{1}}});
    }

    UdpFeedBookBuilder bb;
    replay_into(bb);

    ASSERT_TRUE(bb.is_anchored(kSymbol));
    EXPECT_FALSE(bb.is_stale(kSymbol));
}
