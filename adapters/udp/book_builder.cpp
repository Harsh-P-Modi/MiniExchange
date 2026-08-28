#include "adapters/udp/book_builder.hpp"

#include <cstdio>
#include <cstring>

namespace miniexchange::udp {

// ─────────────────────────────────────────────────────────────────────────────
// Default gap logger: prints to stderr
// ─────────────────────────────────────────────────────────────────────────────

GapLogger default_stderr_gap_logger() {
    return [](SymbolId symbol, uint64_t expected, uint64_t received) {
        std::fprintf(stderr,
                     "[UdpFeedBookBuilder] gap detected: symbol=%u, "
                     "expected_seq=%lu, received_seq=%lu\n",
                     symbol.value,
                     static_cast<unsigned long>(expected),
                     static_cast<unsigned long>(received));
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

UdpFeedBookBuilder::UdpFeedBookBuilder(GapLogger gap_logger)
    : gap_logger_(std::move(gap_logger)) {}

// ─────────────────────────────────────────────────────────────────────────────
// Message dispatch
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedBookBuilder::on_message(const std::byte* data, std::size_t len) {
    if (len < sizeof(FeedHeader)) {
        return;  // too short to be a valid message
    }

    FeedHeader header{};
    std::memcpy(&header, data, sizeof(header));

    switch (header.type) {
        case MessageType::Snapshot: {
            if (len < sizeof(SnapshotMessage)) return;
            SnapshotMessage msg{};
            std::memcpy(&msg, data, sizeof(msg));
            apply_snapshot(msg);
            break;
        }
        case MessageType::TopOfBook: {
            if (len < sizeof(TopOfBookMessage)) return;
            TopOfBookMessage msg{};
            std::memcpy(&msg, data, sizeof(msg));
            apply_top_of_book(msg);
            break;
        }
        case MessageType::Trade: {
            if (len < sizeof(TradeMessage)) return;
            TradeMessage msg{};
            std::memcpy(&msg, data, sizeof(msg));
            apply_trade(msg);
            break;
        }
        default:
            break;  // unknown message type — ignore
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot anchoring (§5, task 16)
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedBookBuilder::apply_snapshot(const SnapshotMessage& msg) {
    auto& state = symbols_[msg.symbol];

    // A snapshot always anchors (or re-anchors) the symbol.
    state.anchored = true;
    state.stale = false;  // snapshot clears staleness
    state.last_sequence = msg.header.sequence;
    state.book = TopOfBook{
        msg.bid_price,
        msg.bid_qty,
        msg.ask_price,
        msg.ask_qty,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Incremental application (task 17)
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedBookBuilder::apply_top_of_book(const TopOfBookMessage& msg) {
    auto it = symbols_.find(msg.symbol);
    if (it == symbols_.end() || !it->second.anchored) {
        return;  // not yet anchored — discard as pre-anchor noise
    }

    auto& state = it->second;

    // Discard messages with sequence <= the anchoring snapshot's sequence
    // (pre-anchor noise that arrived out of order)
    if (msg.header.sequence <= state.last_sequence) {
        return;
    }

    // Check for gaps
    check_sequence(msg.symbol, msg.header.sequence);
    state.last_sequence = msg.header.sequence;

    // Apply the update — including zeroed sides as valid state
    state.book = TopOfBook{
        msg.bid_price,
        msg.bid_qty,
        msg.ask_price,
        msg.ask_qty,
    };
}

void UdpFeedBookBuilder::apply_trade(const TradeMessage& msg) {
    auto it = symbols_.find(msg.symbol);
    if (it == symbols_.end() || !it->second.anchored) {
        return;  // not yet anchored
    }

    auto& state = it->second;

    if (msg.header.sequence <= state.last_sequence) {
        return;  // pre-anchor noise
    }

    // Check for gaps
    check_sequence(msg.symbol, msg.header.sequence);
    state.last_sequence = msg.header.sequence;

    // Trade messages don't directly update the TopOfBook view —
    // the publisher emits a separate TopOfBookMessage for that.
    // The book-builder just tracks the sequence for gap detection.
    // (The subscriber's book state is updated by TopOfBookMessages,
    // not by reconstructing from trades.)
}

// ─────────────────────────────────────────────────────────────────────────────
// Gap detection (task 18)
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedBookBuilder::check_sequence(SymbolId symbol, uint64_t incoming_sequence) {
    auto& state = symbols_[symbol];

    uint64_t expected = state.last_sequence + 1;
    if (incoming_sequence != expected) {
        state.stale = true;
        if (gap_logger_) {
            gap_logger_(symbol, expected, incoming_sequence);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Query interface
// ─────────────────────────────────────────────────────────────────────────────

std::optional<TopOfBook> UdpFeedBookBuilder::top_of_book(SymbolId symbol) const {
    auto it = symbols_.find(symbol);
    if (it == symbols_.end() || !it->second.anchored) {
        return std::nullopt;
    }
    return it->second.book;
}

bool UdpFeedBookBuilder::is_stale(SymbolId symbol) const {
    auto it = symbols_.find(symbol);
    if (it == symbols_.end()) {
        return false;
    }
    return it->second.stale;
}

bool UdpFeedBookBuilder::is_anchored(SymbolId symbol) const {
    auto it = symbols_.find(symbol);
    if (it == symbols_.end()) {
        return false;
    }
    return it->second.anchored;
}

}  // namespace miniexchange::udp
