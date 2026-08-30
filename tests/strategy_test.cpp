// Phase 10 — Strategy SDK tests: market maker quoting + re-quote, momentum
// signal + directional orders, and an extended-session invariant run.

#include <gtest/gtest.h>

#include <vector>

#include "core/Events.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/event_sink.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_engine.hpp"
#include "strategy/MarketMakerStrategy.hpp"
#include "strategy/MomentumStrategy.hpp"

namespace miniexchange {
namespace {

// A sink that records trades so a test can feed them to strategies after
// each engine call (mirrors strategy_runner's deferred dispatch).
class RecordingTradeSink final : public EventSink {
public:
    void on_trade(const Trade& t) override { trades.push_back(t); }
    void on_order_accepted(const OrderAccepted&) override {}
    void on_order_cancelled(const OrderCancelled&) override {}
    std::vector<Trade> trades;
};

// ---------------------------------------------------------------------------
// MarketMakerStrategy (T2, T3, T4)
// ---------------------------------------------------------------------------

TEST(MarketMakerTest, PlacesTwoSidedQuoteOnFirstTick) {
    MatchingEngine engine;
    MarketMakerStrategy mm(engine, ClientId{1},
                           MarketMakerConfig{Price{10000}, Price{5},
                                             Quantity{10}});
    mm.on_tick();

    // A bid at 9995 and an ask at 10005, both size 10, should be resting.
    EXPECT_EQ(engine.book().order_count(), 2u);
    Price best_bid = engine.book().bids().empty()
                         ? Price{0}
                         : engine.book().bids().begin()->first;
    // bids() is a std::map<Price, PriceLevel>; the highest bid is the
    // last key. Just assert both levels exist at the expected prices.
    bool found_bid = false, found_ask = false;
    for (const auto& [px, lvl] : engine.book().bids()) {
        if (px == Price{9995}) found_bid = true;
    }
    for (const auto& [px, lvl] : engine.book().asks()) {
        if (px == Price{10005}) found_ask = true;
    }
    (void)best_bid;
    EXPECT_TRUE(found_bid);
    EXPECT_TRUE(found_ask);
}

TEST(MarketMakerTest, RequotesAfterItsBidIsHit) {
    RecordingTradeSink sink;
    MatchingEngine engine(&sink);
    MarketMakerStrategy mm(engine, ClientId{1},
                           MarketMakerConfig{Price{10000}, Price{5},
                                             Quantity{10}});
    mm.on_tick();  // bid @ 9995, ask @ 10005
    ASSERT_EQ(engine.book().order_count(), 2u);

    // Another participant sells into the MM's bid at 9995 (fully).
    LimitOrder aggressor{OrderId{50000}, Side::Sell, Price{9995}, Quantity{10}};
    aggressor.owner = ClientId{2};
    engine.submit(NewOrder{aggressor});

    // Feed the resulting trade(s) to the strategy (deferred dispatch).
    ASSERT_FALSE(sink.trades.empty());
    for (const auto& t : sink.trades) mm.on_trade(t);

    // The MM should have re-quoted: its old bid was consumed, and it
    // cancels the leftover ask and posts a fresh two-sided quote. Net
    // book state: a fresh bid + fresh ask (2 orders), no stale ask.
    EXPECT_EQ(engine.book().order_count(), 2u);
}

// ---------------------------------------------------------------------------
// MomentumStrategy (T5, T6)
// ---------------------------------------------------------------------------

TEST(MomentumTest, NoSignalBeforeWindowFull) {
    MatchingEngine engine;
    MomentumStrategy mom(engine, ClientId{1},
                         MomentumConfig{/*lookback_n=*/4, Quantity{5},
                                        Price{2}});
    // Feed 3 trades (< lookback of 4): no order should be submitted.
    for (int i = 0; i < 3; ++i) {
        mom.on_trade(Trade{TradeSequence{static_cast<uint64_t>(i)},
                           OrderId{1}, OrderId{2},
                           Price{10000 + i * 5}, Quantity{1}, true});
    }
    EXPECT_EQ(engine.book().order_count(), 0u);
    EXPECT_EQ(mom.window_size(), 3u);
}

TEST(MomentumTest, WindowCapsAtLookback) {
    MatchingEngine engine;
    MomentumStrategy mom(engine, ClientId{1},
                         MomentumConfig{/*lookback_n=*/3, Quantity{5},
                                        Price{100000}});  // huge threshold: never fires
    for (int i = 0; i < 10; ++i) {
        mom.on_trade(Trade{TradeSequence{static_cast<uint64_t>(i)},
                           OrderId{1}, OrderId{2}, Price{10000}, Quantity{1},
                           true});
    }
    EXPECT_EQ(mom.window_size(), 3u);  // never exceeds lookback_n
}

TEST(MomentumTest, BuysOnUpwardMomentum) {
    // Resting sell liquidity for the momentum buy to hit.
    RecordingTradeSink sink;
    MatchingEngine engine(&sink);
    LimitOrder liq{OrderId{50000}, Side::Sell, Price{10050}, Quantity{100}};
    liq.owner = ClientId{9};
    engine.submit(NewOrder{liq});

    MomentumStrategy mom(engine, ClientId{1},
                         MomentumConfig{/*lookback_n=*/3, Quantity{5},
                                        /*signal_threshold=*/Price{2}});
    // Rising prices across the window: 10000 -> 10005 -> 10010 (delta +10 > 2).
    mom.on_trade(Trade{TradeSequence{1}, OrderId{1}, OrderId{2}, Price{10000},
                       Quantity{1}, true});
    mom.on_trade(Trade{TradeSequence{2}, OrderId{1}, OrderId{2}, Price{10005},
                       Quantity{1}, true});
    mom.on_trade(Trade{TradeSequence{3}, OrderId{1}, OrderId{2}, Price{10010},
                       Quantity{1}, true});

    // A buy market order should have been submitted, hitting the resting
    // sell — so a trade occurred where the momentum strategy is the buyer.
    bool momentum_bought = false;
    for (const auto& t : sink.trades) {
        // The momentum strategy's order ids are namespaced high by ClientId.
        if (t.buy_order_id.value >= 1'000'000'000ULL) momentum_bought = true;
    }
    EXPECT_TRUE(momentum_bought);
}

TEST(MomentumTest, SellsOnDownwardMomentum) {
    RecordingTradeSink sink;
    MatchingEngine engine(&sink);
    LimitOrder liq{OrderId{50000}, Side::Buy, Price{9950}, Quantity{100}};
    liq.owner = ClientId{9};
    engine.submit(NewOrder{liq});

    MomentumStrategy mom(engine, ClientId{1},
                         MomentumConfig{/*lookback_n=*/3, Quantity{5},
                                        Price{2}});
    mom.on_trade(Trade{TradeSequence{1}, OrderId{1}, OrderId{2}, Price{10000},
                       Quantity{1}, true});
    mom.on_trade(Trade{TradeSequence{2}, OrderId{1}, OrderId{2}, Price{9995},
                       Quantity{1}, true});
    mom.on_trade(Trade{TradeSequence{3}, OrderId{1}, OrderId{2}, Price{9990},
                       Quantity{1}, true});

    bool momentum_sold = false;
    for (const auto& t : sink.trades) {
        if (t.sell_order_id.value >= 1'000'000'000ULL) momentum_sold = true;
    }
    EXPECT_TRUE(momentum_sold);
}

// ---------------------------------------------------------------------------
// Extended-session invariant test (T11, DoD)
// ---------------------------------------------------------------------------

TEST(StrategySessionTest, ExtendedRunNoCrashNoInvariantViolation) {
    RecordingTradeSink sink;
    RiskConfig risk_config{};
    risk_config.price_band_pct = 0.50;
    risk_config.initial_reference_price = Price{10000};
    risk_config.max_order_qty = Quantity{1'000'000};
    risk_config.tick_size = Price{1};
    risk_config.stp_enabled = true;
    risk_config.stp_policy = StpPolicy::RejectIncoming;

    MatchingEngine matching(&sink, 1'000'000, risk_config.stp());
    RiskEngine risk(&matching, risk_config);
    EngineAPI& engine = risk;

    MarketMakerStrategy mm(engine, ClientId{1001},
                           MarketMakerConfig{Price{10000}, Price{5},
                                             Quantity{10}});
    MomentumStrategy mom(engine, ClientId{1002},
                         MomentumConfig{4, Quantity{5}, Price{2}});
    std::vector<Strategy*> strategies{&mm, &mom};

    // Run an extended session with the same deferred-dispatch loop the
    // runner uses.
    for (int tick = 0; tick < 2000; ++tick) {
        for (Strategy* s : strategies) s->on_tick();
        int guard = 0;
        while (!sink.trades.empty() && guard++ < 1000) {
            std::vector<Trade> batch;
            batch.swap(sink.trades);
            for (const auto& tr : batch) {
                for (Strategy* s : strategies) s->on_trade(tr);
            }
        }
    }

    // Invariant checks (Charter §Invariants, as applicable here):
    //  - the engine never crashed (we got here),
    //  - book order_count is internally consistent: every id found via
    //    find_order is genuinely resting. We spot-check that order_count
    //    matches an independent count of the bid+ask levels' contents.
    std::size_t counted = 0;
    for (const auto& [px, lvl] : engine.book().bids()) {
        (void)px;
        for (const Order* o = lvl.front(); o != nullptr; o = o->next) ++counted;
    }
    for (const auto& [px, lvl] : engine.book().asks()) {
        (void)px;
        for (const Order* o = lvl.front(); o != nullptr; o = o->next) ++counted;
    }
    EXPECT_EQ(counted, engine.book().order_count());
}

}  // namespace
}  // namespace miniexchange
