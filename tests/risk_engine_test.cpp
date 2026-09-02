// Phase 8 — RiskEngine decorator tests.
//
// Covers T7 (pass-through skeleton), T8 (fat-finger R3), T9 (tick-size
// R4), T10 (price-band R2 + reference tracking, incl. cold start), T11
// (distinct rejection reasons R6), and T12 (NFR2: a risk-rejected order
// consumes no OrderId).

#include "risk/risk_engine.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "core/EngineCommand.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/event_sink.hpp"

namespace miniexchange {
namespace {

// A permissive config: every check effectively disabled, so tests can
// enable one rule at a time and be sure nothing else interferes.
RiskConfig permissive_config() {
    RiskConfig c;
    c.price_band_pct = 0.0;  // band check off
    c.initial_reference_price = Price{0};
    c.max_order_qty = Quantity{1'000'000'000};
    c.tick_size = Price{1};  // every integer price is aligned
    c.stp_enabled = false;
    return c;
}

// ---------------------------------------------------------------------------
// T7 — pass-through: with permissive config, wrapped behaves like unwrapped.
// ---------------------------------------------------------------------------

class RiskPassThroughTest : public ::testing::Test {
protected:
    MatchingEngine inner;
    RiskEngine risk{&inner, permissive_config()};
};

TEST_F(RiskPassThroughTest, LimitOrderForwardsAndRests) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.remaining_qty, Quantity{10});
    // Observable through the decorator's book() forwarding.
    EXPECT_EQ(risk.book().order_count(), 1u);
    EXPECT_NE(risk.book().find_order(OrderId{1}), nullptr);
}

TEST_F(RiskPassThroughTest, MatchingStillWorksThroughDecorator) {
    risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{100}, Quantity{10}}});
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{2}, Side::Buy, Price{100}, Quantity{10}}});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    ASSERT_EQ(resp.trades.size(), 1u);
    EXPECT_EQ(resp.trades[0].price, Price{100});
    EXPECT_EQ(risk.book().order_count(), 0u);
}

TEST_F(RiskPassThroughTest, CancelForwards) {
    risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    auto resp = risk.cancel(OrderId{1});

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.remaining_qty, Quantity{10});
    EXPECT_EQ(risk.book().order_count(), 0u);
}

TEST_F(RiskPassThroughTest, UnknownCancelStillReportsUnknown) {
    auto resp = risk.cancel(OrderId{999});
    EXPECT_EQ(resp.status, EngineResult::UnknownOrderId);
}

TEST_F(RiskPassThroughTest, EngineLevelRejectionsPassThroughUnchanged) {
    // Zero quantity is an engine-level rule, not a risk rule — the
    // decorator must not mask it.
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{0}}});
    EXPECT_EQ(resp.status, EngineResult::InvalidQuantity);
}

// ---------------------------------------------------------------------------
// T8 — fat-finger (R3), with the DoD boundary case.
// ---------------------------------------------------------------------------

class FatFingerTest : public ::testing::Test {
protected:
    RiskConfig config = [] {
        RiskConfig c = permissive_config();
        c.max_order_qty = Quantity{100};
        return c;
    }();
    MatchingEngine inner;
    RiskEngine risk{&inner, config};
};

TEST_F(FatFingerTest, BelowMaxAccepted) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{99}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(FatFingerTest, ExactlyAtMaxAccepted) {
    // Boundary: quantity == max must PASS (reject is strictly-greater).
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{100}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(FatFingerTest, AboveMaxRejectedWithSpecificReason) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{101}}});
    EXPECT_EQ(resp.status, EngineResult::QuantityTooLarge);
    EXPECT_TRUE(resp.trades.empty());
    // Never reached the engine.
    EXPECT_EQ(risk.book().order_count(), 0u);
}

TEST_F(FatFingerTest, AppliesToMarketOrdersToo) {
    auto resp =
        risk.submit(NewOrder{MarketOrder{OrderId{1}, Side::Buy, Quantity{101}}});
    EXPECT_EQ(resp.status, EngineResult::QuantityTooLarge);
}

// ---------------------------------------------------------------------------
// T9 — tick size (R4).
// ---------------------------------------------------------------------------

class TickSizeTest : public ::testing::Test {
protected:
    RiskConfig config = [] {
        RiskConfig c = permissive_config();
        c.tick_size = Price{25};
        return c;
    }();
    MatchingEngine inner;
    RiskEngine risk{&inner, config};
};

TEST_F(TickSizeTest, AlignedPriceAccepted) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(TickSizeTest, OneTickOffRejected) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{101}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::TickSizeMisaligned);
    EXPECT_EQ(risk.book().order_count(), 0u);
}

TEST_F(TickSizeTest, LargeExactMultipleAccepted) {
    auto resp = risk.submit(NewOrder{
        LimitOrder{OrderId{1}, Side::Buy, Price{25'000'000}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(TickSizeTest, MarketOrderSkipsTickCheck) {
    // A market order has no price at all, so tick alignment cannot and
    // must not be evaluated for it.
    auto resp =
        risk.submit(NewOrder{MarketOrder{OrderId{1}, Side::Buy, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

// ---------------------------------------------------------------------------
// T10 — price band (R2) + reference tracking, including cold start (Q4).
// ---------------------------------------------------------------------------

class PriceBandTest : public ::testing::Test {
protected:
    RiskConfig config = [] {
        RiskConfig c = permissive_config();
        c.price_band_pct = 0.10;                  // +/-10%
        c.initial_reference_price = Price{1000};  // seeded at construction
        return c;
    }();
    MatchingEngine inner;
    RiskEngine risk{&inner, config};
};

TEST_F(PriceBandTest, ReferenceSeededFromConfig) {
    EXPECT_EQ(risk.reference_price(), Price{1000});
}

TEST_F(PriceBandTest, WithinBandAccepted) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{1050}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(PriceBandTest, ExactlyAtUpperEdgeAccepted) {
    // Boundary: 1000 + 10% = 1100 exactly — must PASS.
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{1100}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(PriceBandTest, ExactlyAtLowerEdgeAccepted) {
    // Boundary: 1000 - 10% = 900 exactly — must PASS.
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{900}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

TEST_F(PriceBandTest, JustAboveUpperEdgeRejected) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Buy, Price{1101}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::PriceOutOfBand);
    EXPECT_EQ(risk.book().order_count(), 0u);
}

TEST_F(PriceBandTest, JustBelowLowerEdgeRejected) {
    auto resp = risk.submit(
        NewOrder{LimitOrder{OrderId{1}, Side::Sell, Price{899}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::PriceOutOfBand);
}

// Q4's whole point: the band is enforced on an EMPTY book with NO trades
// yet, using the seeded static reference — not silently skipped.
TEST_F(PriceBandTest, ColdStartEmptyBookStillEnforcesBand) {
    ASSERT_EQ(risk.book().order_count(), 0u);  // nothing resting
    ASSERT_TRUE(risk.reference_price() == Price{1000});

    auto resp = risk.submit(NewOrder{
        LimitOrder{OrderId{1}, Side::Buy, Price{5000}, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::PriceOutOfBand);
}

TEST_F(PriceBandTest, MarketOrderSkipsBandCheck) {
    auto resp =
        risk.submit(NewOrder{MarketOrder{OrderId{1}, Side::Buy, Quantity{10}}});
    EXPECT_EQ(resp.status, EngineResult::Accepted);
}

// Once a trade happens, the reference tracks the last trade price.
TEST_F(PriceBandTest, ReferenceTracksLastTradePrice) {
    // Rest a sell at 1050 (within band), then buy into it at 1050.
    risk.submit(NewOrder{
        LimitOrder{OrderId{1}, Side::Sell, Price{1050}, Quantity{10}}});
    auto resp = risk.submit(NewOrder{
        LimitOrder{OrderId{2}, Side::Buy, Price{1050}, Quantity{10}}});
    ASSERT_EQ(resp.trades.size(), 1u);
    ASSERT_EQ(resp.trades[0].price, Price{1050});

    // Reference moved from 1000 to the last trade price.
    EXPECT_EQ(risk.reference_price(), Price{1050});

    // The band is now centred on 1050: 1155 is the new upper edge.
    auto edge = risk.submit(NewOrder{
        LimitOrder{OrderId{3}, Side::Buy, Price{1155}, Quantity{10}}});
    EXPECT_EQ(edge.status, EngineResult::Accepted);

    auto beyond = risk.submit(NewOrder{
        LimitOrder{OrderId{4}, Side::Buy, Price{1156}, Quantity{10}}});
    EXPECT_EQ(beyond.status, EngineResult::PriceOutOfBand);
}

// ---------------------------------------------------------------------------
// T11 — each rule maps to its own distinct EngineResult (R6).
// ---------------------------------------------------------------------------

TEST(RiskRejectionReasonsTest, EachRuleHasADistinctReason) {
    RiskConfig c;
    c.price_band_pct = 0.10;
    c.initial_reference_price = Price{1000};
    c.max_order_qty = Quantity{100};
    c.tick_size = Price{10};

    MatchingEngine inner;
    RiskEngine risk{&inner, c};

    // Fat finger (checked first): qty over the ceiling.
    EXPECT_EQ(risk.submit(NewOrder{LimitOrder{OrderId{1}, Side::Buy,
                                              Price{1000}, Quantity{500}}})
                  .status,
              EngineResult::QuantityTooLarge);

    // Tick size: aligned to 10 required, 1005 is not.
    EXPECT_EQ(risk.submit(NewOrder{LimitOrder{OrderId{2}, Side::Buy,
                                              Price{1005}, Quantity{10}}})
                  .status,
              EngineResult::TickSizeMisaligned);

    // Price band: 2000 is aligned to 10 and within qty, but out of band.
    EXPECT_EQ(risk.submit(NewOrder{LimitOrder{OrderId{3}, Side::Buy,
                                              Price{2000}, Quantity{10}}})
                  .status,
              EngineResult::PriceOutOfBand);

    // All four Phase 8 reasons are distinct enum values.
    EXPECT_NE(EngineResult::PriceOutOfBand, EngineResult::QuantityTooLarge);
    EXPECT_NE(EngineResult::QuantityTooLarge,
              EngineResult::TickSizeMisaligned);
    EXPECT_NE(EngineResult::TickSizeMisaligned,
              EngineResult::SelfTradePrevented);
}

// ---------------------------------------------------------------------------
// T12 — NFR2: a risk-rejected order consumes NO OrderId.
// ---------------------------------------------------------------------------

class ReusedOrderIdTest : public ::testing::Test {
protected:
    RiskConfig config = [] {
        RiskConfig c = permissive_config();
        c.max_order_qty = Quantity{100};
        c.tick_size = Price{10};
        c.price_band_pct = 0.10;
        c.initial_reference_price = Price{1000};
        return c;
    }();
    MatchingEngine inner;
    RiskEngine risk{&inner, config};
};

TEST_F(ReusedOrderIdTest, IdReusableAfterFatFingerRejection) {
    auto rejected = risk.submit(NewOrder{
        LimitOrder{OrderId{42}, Side::Buy, Price{1000}, Quantity{500}}});
    ASSERT_EQ(rejected.status, EngineResult::QuantityTooLarge);

    // Same OrderId, now valid — must be ACCEPTED, proving the rejection
    // never advanced the engine's per-client monotonic-ID watermark.
    auto accepted = risk.submit(NewOrder{
        LimitOrder{OrderId{42}, Side::Buy, Price{1000}, Quantity{10}}});
    EXPECT_EQ(accepted.status, EngineResult::Accepted);
}

// Phase 11 R2: a pre-trade risk rejection carries the rejected order's id.
TEST_F(ReusedOrderIdTest, R2_RiskRejectionCarriesOrderId) {
    auto rejected = risk.submit(NewOrder{
        LimitOrder{OrderId{4242}, Side::Buy, Price{1000}, Quantity{500}}});
    ASSERT_EQ(rejected.status, EngineResult::QuantityTooLarge);
    EXPECT_EQ(rejected.order_id, OrderId{4242});

    // Also on the market-order path (fat-finger only).
    auto rejected_mkt = risk.submit(
        NewOrder{MarketOrder{OrderId{4343}, Side::Sell, Quantity{500}}});
    ASSERT_EQ(rejected_mkt.status, EngineResult::QuantityTooLarge);
    EXPECT_EQ(rejected_mkt.order_id, OrderId{4343});
}

TEST_F(ReusedOrderIdTest, IdReusableAfterTickSizeRejection) {
    auto rejected = risk.submit(NewOrder{
        LimitOrder{OrderId{43}, Side::Buy, Price{1005}, Quantity{10}}});
    ASSERT_EQ(rejected.status, EngineResult::TickSizeMisaligned);

    auto accepted = risk.submit(NewOrder{
        LimitOrder{OrderId{43}, Side::Buy, Price{1000}, Quantity{10}}});
    EXPECT_EQ(accepted.status, EngineResult::Accepted);
}

TEST_F(ReusedOrderIdTest, IdReusableAfterPriceBandRejection) {
    auto rejected = risk.submit(NewOrder{
        LimitOrder{OrderId{44}, Side::Buy, Price{2000}, Quantity{10}}});
    ASSERT_EQ(rejected.status, EngineResult::PriceOutOfBand);

    auto accepted = risk.submit(NewOrder{
        LimitOrder{OrderId{44}, Side::Buy, Price{1000}, Quantity{10}}});
    EXPECT_EQ(accepted.status, EngineResult::Accepted);
}

// Contrast: an ID consumed by a genuinely ACCEPTED order is NOT reusable.
// This proves the reused-ID tests above are meaningful and not just
// "duplicate detection is broken."
TEST_F(ReusedOrderIdTest, IdNotReusableAfterAcceptance) {
    auto accepted = risk.submit(NewOrder{
        LimitOrder{OrderId{45}, Side::Buy, Price{1000}, Quantity{10}}});
    ASSERT_EQ(accepted.status, EngineResult::Accepted);

    auto duplicate = risk.submit(NewOrder{
        LimitOrder{OrderId{45}, Side::Buy, Price{1000}, Quantity{10}}});
    EXPECT_EQ(duplicate.status, EngineResult::DuplicateOrderId);
}

// ---------------------------------------------------------------------------
// STP policy pass-down: RiskConfig::stp() produces the engine's StpConfig.
// (The STP behaviour itself is tested in matching_engine_test.cpp — it
// executes in the engine, not the decorator. See design.md §5.)
// ---------------------------------------------------------------------------

TEST(RiskConfigStpTest, StpSliceReflectsConfig) {
    RiskConfig c;
    c.stp_enabled = true;
    c.stp_policy = StpPolicy::CancelResting;

    StpConfig s = c.stp();
    EXPECT_TRUE(s.enabled);
    EXPECT_EQ(s.policy, StpPolicy::CancelResting);

    RiskConfig d;  // defaults
    EXPECT_FALSE(d.stp().enabled);
    EXPECT_EQ(d.stp().policy, StpPolicy::RejectIncoming);
}

// ---------------------------------------------------------------------------
// T13 — composition-root integration: the full RiskEngine -> MatchingEngine
// stack reached only through an EngineAPI& (exactly how
// apps/exchange_server/main.cpp wires it), driven by TaggedCommands the
// way the engine thread does. Verifies each rejection type end-to-end and
// that rejections have zero effect on book state.
//
// The exchange_server app itself is Linux-only (epoll/eventfd), so this
// reproduces its composition and dispatch rather than launching it.
// ---------------------------------------------------------------------------

// Mirrors apps/exchange_server/main.cpp's engine-thread dispatch: tag the
// order with the submitting client, then submit through the EngineAPI.
EngineResponse dispatch_through(EngineAPI& api, ClientId client,
                                EngineCommand& command) {
    return std::visit(
        [&](auto& cmd) -> EngineResponse {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, LimitOrder> ||
                          std::is_same_v<T, MarketOrder>) {
                cmd.owner = client;
                return api.submit(NewOrder{cmd});
            } else {
                static_assert(std::is_same_v<T, CancelRequest>);
                return api.cancel(cmd.id);
            }
        },
        command);
}

class CompositionRootTest : public ::testing::Test {
protected:
    RiskConfig config = [] {
        RiskConfig c;
        c.price_band_pct = 0.10;
        c.initial_reference_price = Price{10000};
        c.max_order_qty = Quantity{1000};
        c.tick_size = Price{10};
        c.stp_enabled = true;
        c.stp_policy = StpPolicy::RejectIncoming;
        return c;
    }();

    MatchingEngine matching{NullEventSink::instance(), 1'000'000,
                            config.stp()};
    RiskEngine risk{&matching, config};
    // Everything downstream sees only the port — the decorator is the
    // single entry point, so nothing can bypass the risk checks.
    EngineAPI& engine{risk};
};

TEST_F(CompositionRootTest, ValidOrderFlowsThroughBothLayers) {
    EngineCommand cmd = LimitOrder{OrderId{1}, Side::Buy, Price{10000},
                                   Quantity{10}};
    auto resp = dispatch_through(engine, ClientId{1}, cmd);

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(engine.book().order_count(), 1u);
    // Owner threaded all the way to the resting order (T3 + T13).
    Order* resting = engine.book().find_order(OrderId{1});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->owner, ClientId{1});
}

TEST_F(CompositionRootTest, EachRejectionTypeReachesTheClientAndLeavesBookClean) {
    // Fat finger.
    EngineCommand big = LimitOrder{OrderId{1}, Side::Buy, Price{10000},
                                    Quantity{5000}};
    EXPECT_EQ(dispatch_through(engine, ClientId{1}, big).status,
              EngineResult::QuantityTooLarge);

    // Tick size (10005 is not a multiple of 10).
    EngineCommand offtick = LimitOrder{OrderId{2}, Side::Buy, Price{10005},
                                        Quantity{10}};
    EXPECT_EQ(dispatch_through(engine, ClientId{1}, offtick).status,
              EngineResult::TickSizeMisaligned);

    // Price band (20000 is aligned and small enough, but way out of band).
    EngineCommand wide = LimitOrder{OrderId{3}, Side::Buy, Price{20000},
                                     Quantity{10}};
    EXPECT_EQ(dispatch_through(engine, ClientId{1}, wide).status,
              EngineResult::PriceOutOfBand);

    // Not one of them touched the book.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(CompositionRootTest, StpActiveThroughTheStack) {
    // Client 5 rests a sell.
    EngineCommand rest = LimitOrder{OrderId{1}, Side::Sell, Price{10000},
                                     Quantity{10}};
    ASSERT_EQ(dispatch_through(engine, ClientId{5}, rest).status,
              EngineResult::Accepted);

    // Same client tries to cross its own order — STP (executed in the
    // engine, configured via RiskConfig) rejects it.
    EngineCommand self = LimitOrder{OrderId{2}, Side::Buy, Price{10000},
                                     Quantity{10}};
    EXPECT_EQ(dispatch_through(engine, ClientId{5}, self).status,
              EngineResult::SelfTradePrevented);

    // A different client crosses it successfully.
    EngineCommand other = LimitOrder{OrderId{3}, Side::Buy, Price{10000},
                                      Quantity{10}};
    auto resp = dispatch_through(engine, ClientId{6}, other);
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    ASSERT_EQ(resp.trades.size(), 1u);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(CompositionRootTest, RiskRejectedIdStillReusableThroughTheStack) {
    EngineCommand bad = LimitOrder{OrderId{77}, Side::Buy, Price{10005},
                                    Quantity{10}};
    ASSERT_EQ(dispatch_through(engine, ClientId{1}, bad).status,
              EngineResult::TickSizeMisaligned);

    // NFR2 end-to-end: the rejection consumed no OrderId.
    EngineCommand good = LimitOrder{OrderId{77}, Side::Buy, Price{10000},
                                     Quantity{10}};
    EXPECT_EQ(dispatch_through(engine, ClientId{1}, good).status,
              EngineResult::Accepted);
}

TEST_F(CompositionRootTest, CancelStillWorksThroughBothLayers) {
    EngineCommand add = LimitOrder{OrderId{1}, Side::Buy, Price{10000},
                                    Quantity{10}};
    ASSERT_EQ(dispatch_through(engine, ClientId{1}, add).status,
              EngineResult::Accepted);

    EngineCommand cancel = CancelRequest{OrderId{1}};
    auto resp = dispatch_through(engine, ClientId{1}, cancel);
    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

}  // namespace
}  // namespace miniexchange
