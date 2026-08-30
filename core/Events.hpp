#ifndef MINIEXCHANGE_CORE_EVENTS_HPP
#define MINIEXCHANGE_CORE_EVENTS_HPP

#include "Trade.hpp"
#include "Types.hpp"
#include <vector>

namespace miniexchange {

// EngineResult — the status portion of an EngineResponse, used to
// distinguish "what happened" (accepted, rejected with reason) from
// "what was the outcome" (trades, remaining quantity). Separating
// status from the full response allows callers to check just the
// result code without unpacking the entire response struct.
enum class EngineResult {
    Accepted,          // order accepted (may have filled, partially or fully)
    DuplicateOrderId,  // ADD with an OrderId ever previously accepted
                       // (lifetime-unique per requirements.md §2.1)
    UnknownOrderId,    // CANCEL referencing an OrderId not currently resting
    InvalidQuantity,   // qty == 0
    InvalidPrice,      // price <= 0 (limit orders only; market orders
                       // carry no price per the NewOrder variant shape)
    PoolExhausted,     // no free slots in the order pool — reject before
                       // any side effects (Phase 3, requirements.md R4)
    SelfTradePrevented,  // (Phase 8, R5) incoming order would cross a
                         // resting order owned by the same ClientId, and
                         // STP policy is RejectIncoming — rejected before
                         // any mutation, so no OrderId is consumed

    // Phase 8 pre-trade risk rejections (R6). Each rule gets its own
    // value so a client can tell "your price is out of band" from "your
    // tick size is wrong" from "your order is too big". Named after the
    // reason, un-prefixed, matching the convention of the values above.
    PriceOutOfBand,      // R2 — price deviates beyond the configured band
    QuantityTooLarge,    // R3 — quantity exceeds the fat-finger ceiling
    TickSizeMisaligned,  // R4 — price is not a multiple of the tick size
};

// EngineResponse — the synchronous return value from EngineAPI::submit
// and EngineAPI::cancel. This is the per-caller channel: "what happened
// to *my* order?" Returned immediately, before control leaves the call.
// Contrast with EventSink (output port), which is the broadcast channel:
// "what happened to *any* order?" — used by market-data feeds, benchmark
// counters, and other observers that need to see every state change
// regardless of who triggered it.
//
// Decision: keep both channels rather than collapsing into one. Rationale:
// - A caller of submit() needs immediate synchronous feedback without
//   having pre-registered a listener (EngineResponse provides that).
// - A market-data adapter (Phase 6) needs to see trades triggered by
//   *other* participants' orders, not just its own (EventSink provides
//   that).
// Collapsing these would force callers to either poll a global event log
// or implement a full EventSink just to get their own result — both worse
// than two small, focused channels. See requirements.md §4 for the full
// rationale behind this split.
struct EngineResponse {
    EngineResult status;
    std::vector<Trade> trades;  // empty if no match occurred; multiple
                                // entries if the incoming order crossed
                                // multiple resting orders in one submit()
    Quantity remaining_qty;     // 0 if fully filled or fully cancelled;
                                // non-zero if partially filled and resting
                                // (limit orders) or if unfilled and
                                // discarded (market orders per R10)
};

// OrderAccepted — EventSink::on_order_accepted payload. Emitted exactly
// once when an ADD request (limit or market) is accepted by the engine
// (requirements.md R16). Not emitted on rejections (R19). A market order
// that immediately fills still counts as "accepted" — this event signals
// "the engine took ownership of this OrderId," not "the order is now
// resting" (market orders never rest per R9-R10).
struct OrderAccepted {
    OrderId id;
    Side side;
    Quantity quantity;  // the original submitted quantity, before any fills
    Price price;        // the order's limit price (Price{0} for market orders,
                        // which carry no price per the NewOrder variant shape)
};

// OrderCancelled — EventSink::on_order_cancelled payload. Emitted exactly
// once when a resting order is successfully removed via cancel()
// (requirements.md R18). Not emitted if the cancel request references an
// unknown/already-filled/already-cancelled ID (R19 — rejections don't
// trigger events).
struct OrderCancelled {
    OrderId id;
    Quantity remaining_qty;  // the quantity that was still resting at
                             // cancellation time (before removal); useful
                             // for market-data feeds to report "X shares
                             // cancelled at price Y" without needing to
                             // reconstruct the order's last-known state
                             // separately
    Side side;               // which side of the book (needed by feed
                             // publishers to know which best to check)
    Price price;             // the price level the order was resting at
};

// Note: Trade (from Trade.hpp) doubles as the EventSink::on_trade payload
// per the requirements.md §2 design decision — one canonical trade
// representation used everywhere (EngineResponse.trades, EventSink,
// market-data feeds, replay logs), not separate "fill" vs "trade" types.

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_EVENTS_HPP
