#ifndef MINIEXCHANGE_CORE_TRADE_HPP
#define MINIEXCHANGE_CORE_TRADE_HPP

#include "Types.hpp"

namespace miniexchange {

// Trade — the canonical representation of an executed fill between a buy
// and a sell order. Used both in EngineResponse (returned to the caller)
// and as the EventSink::on_trade payload (broadcast to observers).
//
// Decision: buy/sell order IDs are the canonical representation (not
// "aggressor/resting" or "incoming/passive") because trade records must
// be understandable outside the matching engine's internal mechanics —
// CLI output, UDP market data feeds, FIX execution reports, and replay
// logs all care about "which buy crossed which sell," not "which one
// happened to arrive second." See requirements.md §2.
struct Trade {
    TradeSequence trade_sequence;  // unique, monotonic trade ID
    OrderId buy_order_id;          // the buy side of this trade
    OrderId sell_order_id;         // the sell side of this trade
    Price price;                   // execution price (always the resting
                                   // order's price per requirements.md R6)
    Quantity quantity;             // filled quantity for this trade
    bool resting_order_removed;    // true if this trade fully consumed the
                                   // resting (passive) counterparty order,
                                   // so it is no longer in the book after
                                   // this fill; false if the resting order
                                   // was only partially filled and still
                                   // rests. Used by the UDP feed publisher
                                   // (Phase 6) to track order count at the
                                   // best price level without maintaining
                                   // full depth.
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_TRADE_HPP
