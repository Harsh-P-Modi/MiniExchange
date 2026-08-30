#ifndef MINIEXCHANGE_RISK_RISK_CONFIG_HPP
#define MINIEXCHANGE_RISK_RISK_CONFIG_HPP

#include "core/Types.hpp"

namespace miniexchange {

// RiskConfig — the full pre-trade risk rule set, supplied to RiskEngine
// at construction (plain constructor dependency injection, matching the
// Charter's pattern list and TcpServer's configurable-port precedent).
// No config file format: see specs/phase-08-risk-engine/design.md §3.
//
// Single-symbol: this is one flat config, not a per-SymbolId map. The
// project is single-symbol (product.md); building a
// std::unordered_map<SymbolId, RiskConfig> now would be speculative
// complexity. If a later phase goes multi-symbol, this grows into a map
// without changing the pattern.
//
// Note on floating point: price_band_pct is a `double`, and that is
// deliberate and *safe* — RiskConfig lives in risk/, NOT in core/,
// orderbook/, or engine/, so it does not violate the "no floating point
// in the engine core" rule (steering/tech.md). The band comparison is
// performed in the risk layer and only ever yields an accept/reject
// decision; no floating-point value is ever stored on an Order, a Trade,
// or anything the engine sees. Prices remain integer ticks throughout.
struct RiskConfig {
    // R2 — price-band check. Fractional deviation from the reference
    // price that is still allowed, e.g. 0.10 permits +/-10%.
    double price_band_pct = 0.10;

    // R2 / Q4 — cold-start reference price. REQUIRED to be meaningful:
    // seeding the reference at construction is what makes the band check
    // active from the very first order, before any trade has occurred.
    // Without it, "reference = last trade price" would be undefined on an
    // empty book and the check would silently no-op — explicitly
    // rejected in requirements.md §5 Q4.
    Price initial_reference_price{0};

    // R3 — fat-finger ceiling. An order with quantity strictly greater
    // than this is rejected.
    Quantity max_order_qty{1'000'000};

    // R4 — tick size. A limit price must be an exact multiple of this.
    // Market orders carry no price and skip the check.
    Price tick_size{1};

    // R5 — self-trade prevention. Executed by the engine (not the
    // decorator); RiskEngine forwards this slice down as an StpConfig.
    // See design.md §5 for why STP lives in the match loop.
    bool stp_enabled = false;
    StpPolicy stp_policy = StpPolicy::RejectIncoming;

    // Convenience: the subset the engine needs for STP.
    [[nodiscard]] StpConfig stp() const {
        return StpConfig{stp_enabled, stp_policy};
    }
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_RISK_RISK_CONFIG_HPP
