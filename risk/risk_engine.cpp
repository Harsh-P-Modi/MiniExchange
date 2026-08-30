#include "risk/risk_engine.hpp"

#include <cmath>
#include <cstdlib>
#include <variant>

namespace miniexchange {

RiskEngine::RiskEngine(EngineAPI* inner, RiskConfig config)
    : inner_(inner),
      config_(config),
      reference_price_(config.initial_reference_price) {}

EngineResponse RiskEngine::submit(const NewOrder& order) {
    // Run the pre-trade checks BEFORE touching inner_ (NFR2): a rejected
    // order never reaches MatchingEngine::submit, so it never records its
    // OrderId in ever_seen_ids_ and has zero side effects.
    const EngineResult check = std::visit(
        [this](const auto& o) -> EngineResult {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, LimitOrder>) {
                // Limit order: price-dependent checks apply.
                return run_checks(o.quantity, &o.price);
            } else {
                static_assert(std::is_same_v<T, MarketOrder>);
                // Market order carries no price (structurally — see
                // core/NewOrder.hpp), so tick-size and price-band are
                // skipped; only the fat-finger ceiling applies.
                return run_checks(o.quantity, nullptr);
            }
        },
        order);

    if (check != EngineResult::Accepted) {
        return EngineResponse{check, {}, Quantity{0}};
    }

    // Passed risk: forward to the wrapped engine.
    EngineResponse response = inner_->submit(order);

    // Track the band reference off any trades that resulted (design.md §4).
    update_reference_from(response);

    return response;
}

EngineResponse RiskEngine::cancel(OrderId id) {
    // No pre-trade risk rule applies to cancels — a cancel reduces
    // exposure, so blocking it would be backwards. Forward unchanged.
    return inner_->cancel(id);
}

const OrderBook& RiskEngine::book() const {
    return inner_->book();
}

EngineResult RiskEngine::run_checks(Quantity qty, const Price* price) const {
    // Order matters only for which reason a caller sees first; each check
    // is independent. Cheapest/most-common first.
    if (const EngineResult r = check_fat_finger(qty);
        r != EngineResult::Accepted) {
        return r;
    }
    if (price != nullptr) {
        if (const EngineResult r = check_tick_size(*price);
            r != EngineResult::Accepted) {
            return r;
        }
        if (const EngineResult r = check_price_band(*price);
            r != EngineResult::Accepted) {
            return r;
        }
    }
    return EngineResult::Accepted;
}

EngineResult RiskEngine::check_fat_finger(Quantity qty) const {
    // R3: strictly greater than the ceiling is rejected, so a quantity
    // exactly AT the maximum is accepted (DoD boundary requirement).
    if (qty > config_.max_order_qty) {
        return EngineResult::QuantityTooLarge;
    }
    return EngineResult::Accepted;
}

EngineResult RiskEngine::check_tick_size(Price price) const {
    // A non-positive tick size is a misconfiguration, not client input —
    // there is no meaningful alignment to check against, so skip rather
    // than reject every order.
    if (config_.tick_size.value <= 0) {
        return EngineResult::Accepted;
    }
    // R4: the price must be an exact multiple of the tick. Pure integer
    // arithmetic — no floating point anywhere near a price.
    if (price.value % config_.tick_size.value != 0) {
        return EngineResult::TickSizeMisaligned;
    }
    return EngineResult::Accepted;
}

EngineResult RiskEngine::check_price_band(Price price) const {
    // A non-positive percentage means "no band configured"; a
    // non-positive reference means the band cannot be expressed as a
    // percentage of it. Both are misconfigurations rather than client
    // errors, so skip instead of rejecting everything. Note this is NOT
    // the "silent no-op before the first trade" that Q4 rejected — the
    // reference is seeded at construction precisely so the check IS
    // active on an empty book; reaching this branch means the operator
    // supplied a zero/negative reference or band.
    if (config_.price_band_pct <= 0.0 || reference_price_.value <= 0) {
        return EngineResult::Accepted;
    }

    // Convert the fractional band into an absolute tick allowance once,
    // then compare in integers. This keeps the accept/reject decision
    // free of repeated floating-point comparisons and makes the boundary
    // exact and predictable.
    const double allowance_f =
        static_cast<double>(reference_price_.value) * config_.price_band_pct;
    const int64_t allowance = static_cast<int64_t>(std::llround(allowance_f));

    const int64_t deviation =
        std::abs(price.value - reference_price_.value);

    // R2: strictly outside the band is rejected, so a price exactly AT
    // the band edge is accepted (DoD boundary requirement).
    if (deviation > allowance) {
        return EngineResult::PriceOutOfBand;
    }
    return EngineResult::Accepted;
}

void RiskEngine::update_reference_from(const EngineResponse& response) {
    // design.md §4: the static seed guarantees the band check is active
    // from t=0; once real trades exist, track the most recent trade price
    // so the band follows the market. The last trade in the vector is the
    // most recent (trades are appended in fill order).
    if (!response.trades.empty()) {
        reference_price_ = response.trades.back().price;
    }
}

}  // namespace miniexchange
