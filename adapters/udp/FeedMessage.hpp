#ifndef MINIEXCHANGE_ADAPTERS_UDP_FEED_MESSAGE_HPP
#define MINIEXCHANGE_ADAPTERS_UDP_FEED_MESSAGE_HPP

#include <cstdint>
#include <type_traits>

#include "core/Types.hpp"

namespace miniexchange::udp {

// MessageType — discriminator for the feed-level message envelope.
// All messages on the wire share a common FeedHeader; the type field
// tells the receiver which struct to interpret the payload as.
enum class MessageType : uint8_t {
    TopOfBook = 1,
    Trade     = 2,
    Snapshot  = 3,
};

// FeedHeader — common prefix for every message on the UDP feed.
//
// sequence: monotonic per-publisher counter, incremented once per logical
//   message sent (not per subscriber). Used by subscribers for gap detection
//   (R2/R4). Distinct from core::TradeSequence — see design.md §3.
// timestamp_ns: CLOCK_MONOTONIC nanoseconds at publish time. For latency
//   diagnostics only (subscriber subtracts its own CLOCK_MONOTONIC reading
//   at receipt). NOT wall-clock, NOT a business timestamp.
struct FeedHeader {
    MessageType type;
    uint8_t     _pad[7];       // explicit padding — fills to 8-byte boundary
                               // so sequence starts at a natural uint64_t
                               // alignment. No implicit compiler padding.
    uint64_t    sequence;
    uint64_t    timestamp_ns;
};

// TopOfBookMessage — incremental update of best bid/ask for a symbol.
//
// Sentinel convention: bid_price == Price{0} means "no known best bid"
// (the bid side has drained — see design.md §1b). Same for ask_price.
// A zeroed side is a valid, publishable state — not "missing data."
struct TopOfBookMessage {
    FeedHeader  header;
    SymbolId    symbol;
    Price       bid_price;
    Quantity    bid_qty;
    Price       ask_price;
    Quantity    ask_qty;
};

// TradeMessage — a single executed fill.
//
// trade_sequence is the engine-level trade ordering (core::Trade's own
// counter), distinct from header.sequence (feed-transport-level). See
// design.md §3. resting_order_removed is NOT included on the wire —
// it's publisher-internal state; subscribers only see the resulting
// TopOfBookMessage change (design.md §1b).
struct TradeMessage {
    FeedHeader    header;
    SymbolId      symbol;
    Price         price;
    Quantity      quantity;
    TradeSequence trade_sequence;
};

// SnapshotMessage — periodic full state for late joiners.
//
// A subscriber that joins mid-stream starts uninitialized and waits for
// a SnapshotMessage to anchor from. Incrementals with sequence <=
// as_of_sequence are discarded as pre-anchor noise, not treated as gaps.
// Same zero-sentinel convention as TopOfBookMessage applies.
struct SnapshotMessage {
    FeedHeader  header;
    SymbolId    symbol;
    Price       bid_price;
    Quantity    bid_qty;
    Price       ask_price;
    Quantity    ask_qty;
    uint64_t    as_of_sequence;  // "this snapshot reflects feed state
                                 //  as of sequence N"
};

// Compile-time guarantees: all message types are POD / trivially copyable,
// so they can be memcpy'd directly on/off the wire without serialization.
static_assert(std::is_trivially_copyable_v<FeedHeader>,
              "FeedHeader must be trivially copyable for wire serialization");
static_assert(std::is_trivially_copyable_v<TopOfBookMessage>,
              "TopOfBookMessage must be trivially copyable for wire serialization");
static_assert(std::is_trivially_copyable_v<TradeMessage>,
              "TradeMessage must be trivially copyable for wire serialization");
static_assert(std::is_trivially_copyable_v<SnapshotMessage>,
              "SnapshotMessage must be trivially copyable for wire serialization");

}  // namespace miniexchange::udp

#endif  // MINIEXCHANGE_ADAPTERS_UDP_FEED_MESSAGE_HPP
