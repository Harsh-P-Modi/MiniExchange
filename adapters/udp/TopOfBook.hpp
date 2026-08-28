#ifndef MINIEXCHANGE_ADAPTERS_UDP_TOP_OF_BOOK_HPP
#define MINIEXCHANGE_ADAPTERS_UDP_TOP_OF_BOOK_HPP

#include "core/Types.hpp"

namespace miniexchange::udp {

// TopOfBook — feed-local value type representing the reconstructed
// best bid/ask state for a single symbol.
//
// This is NOT a core domain type — it lives in adapters/udp/ because
// it's the book-builder's output surface, not something the engine
// itself ever produces or consumes.
//
// Sentinel convention (same as TopOfBookMessage / SnapshotMessage):
//   bid_price == Price{0}  →  "no known best bid" (side drained, §1b)
//   ask_price == Price{0}  →  "no known best ask" (side drained, §1b)
// A zeroed side is valid reconstructed state, not "uninitialized."
struct TopOfBook {
    Price    bid_price;
    Quantity bid_qty;
    Price    ask_price;
    Quantity ask_qty;

    constexpr bool operator==(const TopOfBook& other) const {
        return bid_price == other.bid_price &&
               bid_qty == other.bid_qty &&
               ask_price == other.ask_price &&
               ask_qty == other.ask_qty;
    }
    constexpr bool operator!=(const TopOfBook& other) const {
        return !(*this == other);
    }
};

}  // namespace miniexchange::udp

#endif  // MINIEXCHANGE_ADAPTERS_UDP_TOP_OF_BOOK_HPP
