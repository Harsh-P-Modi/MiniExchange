#ifndef MINIEXCHANGE_ADAPTERS_UDP_BOOK_BUILDER_HPP
#define MINIEXCHANGE_ADAPTERS_UDP_BOOK_BUILDER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Types.hpp"

// This file deliberately includes NOTHING from engine/ or orderbook/.
// The BookBuilder reconstructs state from the wire alone (R3).

namespace miniexchange::udp {

// GapLogger — injectable callback invoked when a sequence gap is detected.
// Parameters: symbol, expected sequence, received sequence.
// Defaults to a stderr-printing implementation; tests inject a capturing lambda.
using GapLogger = std::function<void(SymbolId symbol,
                                     uint64_t expected_sequence,
                                     uint64_t received_sequence)>;

// Default gap logger: prints to stderr. Used in production when no
// custom logger is injected.
GapLogger default_stderr_gap_logger();

// UdpFeedBookBuilder — subscriber-side book reconstruction from the UDP feed.
//
// Starts uninitialized; anchors on first SnapshotMessage; applies
// incremental TopOfBook/Trade messages after anchoring; detects gaps
// and flags staleness per design.md §7.
//
// No reference to engine/ is reachable from this class — enforced by
// header inclusion (this file includes only adapters/udp/ and core/).
class UdpFeedBookBuilder {
public:
    explicit UdpFeedBookBuilder(GapLogger gap_logger = default_stderr_gap_logger());

    // Feed raw wire bytes into the builder. Dispatches by FeedHeader::type.
    void on_message(const std::byte* data, std::size_t len);

    // Query the reconstructed top-of-book for a symbol.
    // Returns std::nullopt if the symbol has never been seen or isn't anchored.
    [[nodiscard]] std::optional<TopOfBook> top_of_book(SymbolId symbol) const;

    // Returns true if a gap has been detected since the last snapshot
    // for this symbol (the local state may be stale/inconsistent).
    [[nodiscard]] bool is_stale(SymbolId symbol) const;

    // Returns true if the builder has received at least one snapshot
    // for this symbol and is applying incrementals.
    [[nodiscard]] bool is_anchored(SymbolId symbol) const;

private:
    void apply_snapshot(const SnapshotMessage& msg);
    void apply_top_of_book(const TopOfBookMessage& msg);
    void apply_trade(const TradeMessage& msg);
    void check_sequence(SymbolId symbol, uint64_t incoming_sequence);

    struct PerSymbolState {
        std::optional<TopOfBook> book;
        uint64_t last_sequence = 0;
        bool anchored = false;   // has a snapshot been applied?
        bool stale = false;      // gap detected since last snapshot
    };

    GapLogger gap_logger_;
    std::unordered_map<SymbolId, PerSymbolState> symbols_;
};

}  // namespace miniexchange::udp

#endif  // MINIEXCHANGE_ADAPTERS_UDP_BOOK_BUILDER_HPP
