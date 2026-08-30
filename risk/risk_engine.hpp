#ifndef MINIEXCHANGE_RISK_RISK_ENGINE_HPP
#define MINIEXCHANGE_RISK_RISK_ENGINE_HPP

#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "interfaces/engine_api.hpp"
#include "risk/risk_config.hpp"

namespace miniexchange {

// RiskEngine — a pre-trade risk-checking Decorator over EngineAPI.
//
// Implements EngineAPI itself and holds an injected EngineAPI* inner
// (normally a MatchingEngine). On submit() it runs the configurable
// pre-trade checks and either returns a rejection EngineResult or
// forwards to inner->submit(). Adapters are constructed against
// EngineAPI and cannot tell whether risk checks are enabled (R1).
//
// Scope of this class (design.md §2): the three *stateless, pre-matching*
// checks that depend only on the incoming order's own fields —
//   - R3 fat-finger   (quantity ceiling)
//   - R4 tick-size    (price alignment)
//   - R2 price-band   (deviation from a reference price)
//
// Self-trade prevention (R5) is deliberately NOT here. STP is a question
// about the relationship between the incoming order and resting orders,
// so it executes inside MatchingEngine's match loop; RiskEngine only
// passes the policy down via RiskConfig::stp(). See design.md §5 for the
// full "why not in the decorator" argument.
//
// NFR2: every check runs BEFORE inner->submit() is called, so a rejected
// order never reaches the engine and therefore never consumes its
// OrderId in ever_seen_ids_. RiskEngine never touches ever_seen_ids_
// itself. See design.md §7.
class RiskEngine : public EngineAPI {
public:
    // Constructor injection: the wrapped engine and the rule set.
    // `inner` must outlive this RiskEngine (non-owning pointer, matching
    // the EventSink* convention used by MatchingEngine).
    RiskEngine(EngineAPI* inner, RiskConfig config);

    // EngineAPI implementation.
    EngineResponse submit(const NewOrder& order) override;
    EngineResponse cancel(OrderId id) override;
    [[nodiscard]] const OrderBook& book() const override;

    // Current price-band reference. Seeded from
    // RiskConfig::initial_reference_price, then tracks the last trade
    // price once trading begins (design.md §4).
    [[nodiscard]] Price reference_price() const { return reference_price_; }

private:
    EngineAPI* inner_;
    RiskConfig config_;
    Price reference_price_;

    // The three decorator checks. Each returns EngineResult::Accepted
    // when the order passes, or the specific rejection reason (R6).
    [[nodiscard]] EngineResult check_fat_finger(Quantity qty) const;
    [[nodiscard]] EngineResult check_tick_size(Price price) const;
    [[nodiscard]] EngineResult check_price_band(Price price) const;

    // Runs all applicable checks for a limit order (has a price) or a
    // market order (no price — skips the price-dependent checks).
    [[nodiscard]] EngineResult run_checks(Quantity qty,
                                          const Price* price) const;

    // After a successful forward, advance the band reference to the last
    // trade price observed in the response (design.md §4).
    void update_reference_from(const EngineResponse& response);
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_RISK_RISK_ENGINE_HPP
