#include "adapters/udp/book_builder.hpp"
#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace miniexchange;
using namespace miniexchange::udp;

// ---------------------------------------------------------------------------
// Test fixture: builds wire messages and feeds them into the BookBuilder.
// ---------------------------------------------------------------------------

class BookBuilderTest : public ::testing::Test {
protected:
    static constexpr SymbolId kSymbol{1};

    struct GapRecord {
        SymbolId symbol;
        uint64_t expected;
        uint64_t received;
    };

    std::vector<GapRecord> gap_log;

    GapLogger make_recording_logger() {
        return [this](SymbolId s, uint64_t exp, uint64_t recv) {
            gap_log.push_back({s, exp, recv});
        };
    }

    std::unique_ptr<UdpFeedBookBuilder> make_builder() {
        return std::make_unique<UdpFeedBookBuilder>(make_recording_logger());
    }

    // Helper: build a SnapshotMessage and feed it
    void feed_snapshot(UdpFeedBookBuilder& bb, uint64_t seq,
                       Price bid_p, Quantity bid_q,
                       Price ask_p, Quantity ask_q) {
        SnapshotMessage msg{};
        msg.header.type = MessageType::Snapshot;
        std::memset(msg.header._pad, 0, sizeof(msg.header._pad));
        msg.header.sequence = seq;
        msg.header.timestamp_ns = 0;
        msg.symbol = kSymbol;
        msg.bid_price = bid_p;
        msg.bid_qty = bid_q;
        msg.ask_price = ask_p;
        msg.ask_qty = ask_q;
        msg.as_of_sequence = seq;
        bb.on_message(reinterpret_cast<const std::byte*>(&msg), sizeof(msg));
    }

    // Helper: build a TopOfBookMessage and feed it
    void feed_tob(UdpFeedBookBuilder& bb, uint64_t seq,
                  Price bid_p, Quantity bid_q,
                  Price ask_p, Quantity ask_q) {
        TopOfBookMessage msg{};
        msg.header.type = MessageType::TopOfBook;
        std::memset(msg.header._pad, 0, sizeof(msg.header._pad));
        msg.header.sequence = seq;
        msg.header.timestamp_ns = 0;
        msg.symbol = kSymbol;
        msg.bid_price = bid_p;
        msg.bid_qty = bid_q;
        msg.ask_price = ask_p;
        msg.ask_qty = ask_q;
        bb.on_message(reinterpret_cast<const std::byte*>(&msg), sizeof(msg));
    }

    // Helper: build a TradeMessage and feed it
    void feed_trade(UdpFeedBookBuilder& bb, uint64_t seq,
                    Price price, Quantity qty, TradeSequence ts) {
        TradeMessage msg{};
        msg.header.type = MessageType::Trade;
        std::memset(msg.header._pad, 0, sizeof(msg.header._pad));
        msg.header.sequence = seq;
        msg.header.timestamp_ns = 0;
        msg.symbol = kSymbol;
        msg.price = price;
        msg.quantity = qty;
        msg.trade_sequence = ts;
        bb.on_message(reinterpret_cast<const std::byte*>(&msg), sizeof(msg));
    }
};

// ---------------------------------------------------------------------------
// Task 15: Skeleton — uninitialized state
// ---------------------------------------------------------------------------

TEST_F(BookBuilderTest, InitiallyNotAnchored) {
    auto bb = make_builder();
    EXPECT_FALSE(bb->is_anchored(kSymbol));
    EXPECT_EQ(bb->top_of_book(kSymbol), std::nullopt);
    EXPECT_FALSE(bb->is_stale(kSymbol));
}

// ---------------------------------------------------------------------------
// Task 16: Snapshot anchoring
// ---------------------------------------------------------------------------

TEST_F(BookBuilderTest, SnapshotAnchorsSymbol) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    EXPECT_TRUE(bb->is_anchored(kSymbol));
    auto tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->bid_price, Price{100});
    EXPECT_EQ(tob->bid_qty, Quantity{20});
    EXPECT_EQ(tob->ask_price, Price{101});
    EXPECT_EQ(tob->ask_qty, Quantity{15});
}

TEST_F(BookBuilderTest, IncrementalsBeforeSnapshotIgnored) {
    auto bb = make_builder();

    // Send incrementals before any snapshot — should be ignored
    feed_tob(*bb, 5, Price{99}, Quantity{10}, Price{102}, Quantity{5});
    feed_trade(*bb, 6, Price{101}, Quantity{3}, TradeSequence{1});

    EXPECT_FALSE(bb->is_anchored(kSymbol));
    EXPECT_EQ(bb->top_of_book(kSymbol), std::nullopt);
    EXPECT_TRUE(gap_log.empty());  // no gap logged for unanchored messages
}

TEST_F(BookBuilderTest, IncrementalsWithSequenceLtSnapshotIgnored) {
    auto bb = make_builder();

    // Snapshot at seq 10
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Incremental with seq 8 (pre-anchor noise) — should be ignored
    feed_tob(*bb, 8, Price{99}, Quantity{5}, Price{102}, Quantity{3});

    auto tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->bid_price, Price{100});  // unchanged from snapshot
    EXPECT_TRUE(gap_log.empty());  // not a gap, just noise
}

// ---------------------------------------------------------------------------
// Task 17: Incremental application
// ---------------------------------------------------------------------------

TEST_F(BookBuilderTest, TopOfBookIncrementalUpdatesState) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Incremental at seq 11 — new top of book
    feed_tob(*bb, 11, Price{100}, Quantity{25}, Price{101}, Quantity{12});

    auto tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->bid_qty, Quantity{25});
    EXPECT_EQ(tob->ask_qty, Quantity{12});
}

TEST_F(BookBuilderTest, ZeroedSideStoredAsValidState) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Bid side drains
    feed_tob(*bb, 11, Price{0}, Quantity{0}, Price{101}, Quantity{15});

    auto tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->bid_price, Price{0});
    EXPECT_EQ(tob->bid_qty, Quantity{0});
    EXPECT_EQ(tob->ask_price, Price{101});
}

TEST_F(BookBuilderTest, TradeDoesNotUpdateBook) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Trade at seq 11 — book state should NOT change
    feed_trade(*bb, 11, Price{101}, Quantity{5}, TradeSequence{1});

    auto tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->ask_qty, Quantity{15});  // unchanged
}

// ---------------------------------------------------------------------------
// Task 18: Gap detection and staleness
// ---------------------------------------------------------------------------

TEST_F(BookBuilderTest, GapDetectedOnNonContiguousSequence) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Skip seq 11, send seq 12 — gap!
    feed_tob(*bb, 12, Price{100}, Quantity{18}, Price{101}, Quantity{15});

    EXPECT_TRUE(bb->is_stale(kSymbol));
    ASSERT_EQ(gap_log.size(), 1u);
    EXPECT_EQ(gap_log[0].symbol, kSymbol);
    EXPECT_EQ(gap_log[0].expected, 11u);
    EXPECT_EQ(gap_log[0].received, 12u);
}

TEST_F(BookBuilderTest, StalenessClearsOnlyOnSnapshot) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Gap: skip 11
    feed_tob(*bb, 12, Price{100}, Quantity{18}, Price{101}, Quantity{15});
    EXPECT_TRUE(bb->is_stale(kSymbol));

    // Resumed sequence (13 follows 12 correctly) — staleness NOT cleared
    feed_tob(*bb, 13, Price{100}, Quantity{16}, Price{101}, Quantity{15});
    EXPECT_TRUE(bb->is_stale(kSymbol));

    // New snapshot clears staleness
    feed_snapshot(*bb, 20, Price{100}, Quantity{16}, Price{101}, Quantity{15});
    EXPECT_FALSE(bb->is_stale(kSymbol));
}

TEST_F(BookBuilderTest, MultipleGapsLogged) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    feed_tob(*bb, 12, Price{100}, Quantity{18}, Price{101}, Quantity{15});  // gap: 11 missing
    feed_tob(*bb, 15, Price{100}, Quantity{16}, Price{101}, Quantity{15});  // gap: 13,14 missing

    ASSERT_EQ(gap_log.size(), 2u);
    EXPECT_EQ(gap_log[0].expected, 11u);
    EXPECT_EQ(gap_log[0].received, 12u);
    EXPECT_EQ(gap_log[1].expected, 13u);
    EXPECT_EQ(gap_log[1].received, 15u);
}

TEST_F(BookBuilderTest, NormalSequenceNoGap) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    feed_tob(*bb, 11, Price{100}, Quantity{18}, Price{101}, Quantity{15});
    feed_trade(*bb, 12, Price{101}, Quantity{5}, TradeSequence{1});
    feed_tob(*bb, 13, Price{100}, Quantity{18}, Price{101}, Quantity{10});

    EXPECT_FALSE(bb->is_stale(kSymbol));
    EXPECT_TRUE(gap_log.empty());
}

// ---------------------------------------------------------------------------
// Edge case: drained side followed by snapshot recovery
// ---------------------------------------------------------------------------

TEST_F(BookBuilderTest, DrainedSideRecoveredBySnapshot) {
    auto bb = make_builder();
    feed_snapshot(*bb, 10, Price{100}, Quantity{20}, Price{101}, Quantity{15});

    // Bid drains
    feed_tob(*bb, 11, Price{0}, Quantity{0}, Price{101}, Quantity{15});
    auto tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->bid_price, Price{0});

    // Snapshot re-establishes bid
    feed_snapshot(*bb, 20, Price{99}, Quantity{10}, Price{101}, Quantity{15});
    tob = bb->top_of_book(kSymbol);
    ASSERT_TRUE(tob.has_value());
    EXPECT_EQ(tob->bid_price, Price{99});
    EXPECT_EQ(tob->bid_qty, Quantity{10});
}
