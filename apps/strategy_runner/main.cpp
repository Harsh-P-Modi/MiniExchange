// apps/strategy_runner/main.cpp — composition root for the Strategy SDK
// (Phase 10, design.md §5).
//
// Wires one or more Strategy instances to a live engine, in-process,
// through the EngineAPI port (design.md Q1: in-process is the baseline).
// Because it's built against EngineAPI, it runs the strategies through
// Phase 8's RiskEngine decorator exactly as the TCP/FIX gateways do
// (NFR1).
//
// Re-entrancy note: the engine calls EventSink::on_trade DURING submit().
// If a strategy submitted an order directly from inside on_trade, that
// would recurse into the engine while an outer submit() is still on the
// stack. To keep engine calls strictly non-reentrant, the runner's sink
// only RECORDS trades during a submit; the runner then dispatches those
// recorded trades to the strategies AFTER the triggering call returns,
// on the runner's own stack frame. Strategy order submissions therefore
// always happen at the top level, never nested inside another submit.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core/Events.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/event_sink.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_engine.hpp"
#include "strategy/MarketMakerStrategy.hpp"
#include "strategy/MomentumStrategy.hpp"
#include "strategy/Strategy.hpp"

namespace {

using namespace miniexchange;

// Collects trades emitted during engine calls so they can be dispatched
// to strategies AFTER the triggering call returns (see re-entrancy note).
class DeferredTradeSink final : public EventSink {
public:
    void on_trade(const Trade& trade) override { pending_.push_back(trade); }
    void on_order_accepted(const OrderAccepted&) override {}
    void on_order_cancelled(const OrderCancelled&) override {}

    // Move out the accumulated trades, clearing the buffer.
    std::vector<Trade> drain() {
        std::vector<Trade> out;
        out.swap(pending_);
        return out;
    }

    [[nodiscard]] bool empty() const { return pending_.empty(); }

private:
    std::vector<Trade> pending_;
};

struct RunnerConfig {
    bool run_market_maker = true;
    bool run_momentum = true;
    std::size_t ticks = 5000;  // "extended session" length (DoD)
};

RunnerConfig parse_args(int argc, char** argv) {
    RunnerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string_view a(argv[i]);
        if (a == "--mm-only") {
            cfg.run_market_maker = true;
            cfg.run_momentum = false;
        } else if (a == "--momentum-only") {
            cfg.run_market_maker = false;
            cfg.run_momentum = true;
        } else if (a.starts_with("--ticks=")) {
            cfg.ticks = static_cast<std::size_t>(
                std::atoll(a.substr(8).data()));
        }
    }
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace miniexchange;

    const RunnerConfig run_cfg = parse_args(argc, argv);

    // --- Engine + risk layer (built against EngineAPI, per NFR1) ---
    DeferredTradeSink sink;
    RiskConfig risk_config{};
    risk_config.price_band_pct = 0.50;  // wide band — strategies roam
    risk_config.initial_reference_price = Price{10000};
    risk_config.max_order_qty = Quantity{1'000'000};
    risk_config.tick_size = Price{1};
    risk_config.stp_enabled = true;
    risk_config.stp_policy = StpPolicy::RejectIncoming;

    MatchingEngine matching(&sink, 1'000'000, risk_config.stp());
    RiskEngine risk(&matching, risk_config);
    EngineAPI& engine = risk;

    // --- Strategies (each a distinct persistent ClientId) ---
    std::vector<Strategy*> strategies;

    MarketMakerConfig mm_config{};
    mm_config.reference_price = Price{10000};
    mm_config.spread = Price{5};
    mm_config.quote_size = Quantity{10};
    mm_config.drift_per_tick = 2;  // oscillate so quotes cross -> trades -> flow
    MarketMakerStrategy mm(engine, ClientId{1001}, mm_config);
    MomentumConfig mom_config{};
    mom_config.lookback_n = 4;
    mom_config.order_size = Quantity{5};
    mom_config.signal_threshold = Price{2};
    mom_config.probe_every_ticks = 3;  // seed the tape so flow bootstraps
    mom_config.probe_size = Quantity{2};
    MomentumStrategy mom(engine, ClientId{1002}, mom_config);

    if (run_cfg.run_market_maker) strategies.push_back(&mm);
    if (run_cfg.run_momentum) strategies.push_back(&mom);

    if (strategies.empty()) {
        std::fprintf(stderr, "no strategies selected\n");
        return 1;
    }

    // --- Drive the session ---
    // Each tick: nudge every strategy (on_tick), then drain any trades
    // that resulted and dispatch them to every strategy's on_trade. The
    // drain-then-dispatch loop continues until no new trades are produced
    // (a momentum order can trigger a fill that feeds another signal),
    // so a single tick fully settles before the next.
    std::uint64_t total_trades = 0;
    for (std::size_t t = 0; t < run_cfg.ticks; ++t) {
        for (Strategy* s : strategies) {
            s->on_tick();
        }
        // Settle all cascading trades produced this tick.
        int guard = 0;
        while (!sink.empty() && guard++ < 1000) {
            std::vector<Trade> trades = sink.drain();
            total_trades += trades.size();
            for (const Trade& tr : trades) {
                for (Strategy* s : strategies) {
                    s->on_trade(tr);
                }
            }
        }
    }

    std::printf("strategy_runner: %zu ticks, %llu trades, final book order_count=%zu\n",
                run_cfg.ticks,
                static_cast<unsigned long long>(total_trades),
                engine.book().order_count());
    return 0;
}
