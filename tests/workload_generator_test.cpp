#include <gtest/gtest.h>

#include <set>
#include <unordered_set>
#include <variant>

#include "tools/workload_generator/workload_generator.hpp"

using namespace miniexchange;

namespace {

// Default config for most tests
WorkloadConfig make_default_config(uint64_t seed = 42) {
    return WorkloadConfig{
        .seed = seed,
        .mid_price = Price{1000},
        .price_stddev_log = 0.5,
        .quantity_min = Quantity{1},
        .quantity_max = Quantity{100},
        .add_limit_ratio = 0.5,
        .add_market_ratio = 0.2,
        .cancel_ratio = 0.3,
    };
}

}  // namespace

// R5 reproducibility: same seed produces identical sequences
TEST(WorkloadGeneratorTest, SameSeedProducesIdenticalSequence) {
    WorkloadConfig config = make_default_config(123);

    WorkloadGenerator gen1(config);
    WorkloadGenerator gen2(config);

    auto events1 = gen1.generate(1000);
    auto events2 = gen2.generate(1000);

    ASSERT_EQ(events1.size(), events2.size());

    for (size_t i = 0; i < events1.size(); ++i) {
        ASSERT_EQ(events1[i].index(), events2[i].index())
            << "Event type mismatch at index " << i;

        std::visit(
            [&](const auto& e1) {
                using T = std::decay_t<decltype(e1)>;
                const auto& e2 = std::get<T>(events2[i]);

                if constexpr (std::is_same_v<T, LimitOrder>) {
                    EXPECT_EQ(e1.id, e2.id);
                    EXPECT_EQ(e1.side, e2.side);
                    EXPECT_EQ(e1.price, e2.price);
                    EXPECT_EQ(e1.quantity, e2.quantity);
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    EXPECT_EQ(e1.id, e2.id);
                    EXPECT_EQ(e1.side, e2.side);
                    EXPECT_EQ(e1.quantity, e2.quantity);
                } else if constexpr (std::is_same_v<T, CancelRequest>) {
                    EXPECT_EQ(e1.id, e2.id);
                }
            },
            events1[i]);
    }
}

// Different seed produces different sequences
TEST(WorkloadGeneratorTest, DifferentSeedProducesDifferentSequence) {
    WorkloadConfig config1 = make_default_config(42);
    WorkloadConfig config2 = make_default_config(99);

    WorkloadGenerator gen1(config1);
    WorkloadGenerator gen2(config2);

    auto events1 = gen1.generate(100);
    auto events2 = gen2.generate(100);

    // With different seeds, at least some events should differ.
    // Check that sequences are not identical.
    bool any_difference = false;
    for (size_t i = 0; i < events1.size(); ++i) {
        if (events1[i].index() != events2[i].index()) {
            any_difference = true;
            break;
        }
        std::visit(
            [&](const auto& e1) {
                using T = std::decay_t<decltype(e1)>;
                if (!std::holds_alternative<T>(events2[i])) {
                    any_difference = true;
                    return;
                }
                const auto& e2 = std::get<T>(events2[i]);

                if constexpr (std::is_same_v<T, LimitOrder>) {
                    if (e1.price != e2.price || e1.quantity != e2.quantity ||
                        e1.side != e2.side) {
                        any_difference = true;
                    }
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    if (e1.quantity != e2.quantity || e1.side != e2.side) {
                        any_difference = true;
                    }
                } else if constexpr (std::is_same_v<T, CancelRequest>) {
                    if (e1.id != e2.id) {
                        any_difference = true;
                    }
                }
            },
            events1[i]);
        if (any_difference) break;
    }

    EXPECT_TRUE(any_difference)
        << "Two different seeds produced identical sequences — extremely "
           "unlikely unless seeding is broken";
}

// CANCEL events only reference OrderIds from previously generated LimitOrders
TEST(WorkloadGeneratorTest, CancelOnlyReferencesLimitOrderIds) {
    WorkloadConfig config = make_default_config(777);
    WorkloadGenerator gen(config);

    auto events = gen.generate(10000);

    // Track all OrderIds produced by LimitOrders
    std::unordered_set<uint64_t> limit_order_ids;
    // Track all OrderIds produced by MarketOrders
    std::unordered_set<uint64_t> market_order_ids;

    for (const auto& event : events) {
        std::visit(
            [&](const auto& e) {
                using T = std::decay_t<decltype(e)>;

                if constexpr (std::is_same_v<T, LimitOrder>) {
                    limit_order_ids.insert(e.id.value);
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    market_order_ids.insert(e.id.value);
                } else if constexpr (std::is_same_v<T, CancelRequest>) {
                    // Must reference a previously issued LimitOrder ID
                    EXPECT_TRUE(limit_order_ids.count(e.id.value) > 0)
                        << "CancelRequest references OrderId "
                        << e.id.value
                        << " which was not generated as a LimitOrder";

                    // Must NEVER reference a MarketOrder ID
                    EXPECT_TRUE(market_order_ids.count(e.id.value) == 0)
                        << "CancelRequest references OrderId "
                        << e.id.value
                        << " which was generated as a MarketOrder";
                }
            },
            event);
    }
}

// Mix ratio statistical check (±5% tolerance)
TEST(WorkloadGeneratorTest, MixRatioMatchesConfig) {
    WorkloadConfig config = make_default_config(55);
    WorkloadGenerator gen(config);

    const size_t count = 100000;
    auto events = gen.generate(count);

    size_t limit_count = 0;
    size_t market_count = 0;
    size_t cancel_count = 0;

    for (const auto& event : events) {
        std::visit(
            [&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, LimitOrder>) {
                    ++limit_count;
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    ++market_count;
                } else if constexpr (std::is_same_v<T, CancelRequest>) {
                    ++cancel_count;
                }
            },
            event);
    }

    double limit_ratio =
        static_cast<double>(limit_count) / static_cast<double>(count);
    double market_ratio =
        static_cast<double>(market_count) / static_cast<double>(count);
    double cancel_ratio =
        static_cast<double>(cancel_count) / static_cast<double>(count);

    // Note: cancel fallback (when assumed_resting_ is empty) converts
    // some would-be cancels into LimitOrders. The actual limit ratio
    // will be >= config's limit ratio, and actual cancel ratio will be
    // <= config's cancel ratio. We account for this by checking that
    // limit + cancel combined is within tolerance of their combined
    // config ratio, and market is independent.
    const double tolerance = 0.05;

    // Market orders are never affected by the fallback mechanism
    EXPECT_NEAR(market_ratio, config.add_market_ratio, tolerance)
        << "Market order ratio " << market_ratio
        << " deviates from configured " << config.add_market_ratio;

    // Limit + cancel combined should match their combined config ratio
    double combined_actual = limit_ratio + cancel_ratio;
    double combined_config = config.add_limit_ratio + config.cancel_ratio;
    EXPECT_NEAR(combined_actual, combined_config, tolerance)
        << "Combined limit+cancel ratio " << combined_actual
        << " deviates from configured " << combined_config;

    // Cancel ratio should be at most the configured ratio (fallback
    // reduces it), and limit ratio should be at least the configured
    // ratio (fallback increases it)
    EXPECT_LE(cancel_ratio, config.cancel_ratio + tolerance);
    EXPECT_GE(limit_ratio, config.add_limit_ratio - tolerance);
}

// Verify generated prices are always positive
TEST(WorkloadGeneratorTest, PricesAlwaysPositive) {
    WorkloadConfig config = make_default_config(1234);
    WorkloadGenerator gen(config);

    auto events = gen.generate(10000);

    for (const auto& event : events) {
        if (std::holds_alternative<LimitOrder>(event)) {
            const auto& order = std::get<LimitOrder>(event);
            EXPECT_GT(order.price.value, 0)
                << "Generated a non-positive price: " << order.price.value;
        }
    }
}

// Verify OrderIds are monotonically increasing and unique
TEST(WorkloadGeneratorTest, OrderIdsMonotonicallyIncreasing) {
    WorkloadConfig config = make_default_config(321);
    WorkloadGenerator gen(config);

    auto events = gen.generate(5000);

    uint64_t last_id = 0;
    std::unordered_set<uint64_t> seen_ids;

    for (const auto& event : events) {
        std::visit(
            [&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, LimitOrder>) {
                    EXPECT_GT(e.id.value, last_id);
                    EXPECT_TRUE(seen_ids.insert(e.id.value).second)
                        << "Duplicate OrderId: " << e.id.value;
                    last_id = e.id.value;
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    EXPECT_GT(e.id.value, last_id);
                    EXPECT_TRUE(seen_ids.insert(e.id.value).second)
                        << "Duplicate OrderId: " << e.id.value;
                    last_id = e.id.value;
                }
                // CancelRequests reference existing IDs, not new ones
            },
            event);
    }
}

// Verify all generated quantities are within [quantity_min, quantity_max]
TEST(WorkloadGeneratorTest, QuantitiesWithinConfiguredRange) {
    WorkloadConfig config = make_default_config(4567);
    WorkloadGenerator gen(config);

    auto events = gen.generate(10000);

    for (const auto& event : events) {
        std::visit(
            [&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, LimitOrder>) {
                    EXPECT_GE(e.quantity.value, config.quantity_min.value)
                        << "LimitOrder quantity " << e.quantity.value
                        << " below minimum " << config.quantity_min.value;
                    EXPECT_LE(e.quantity.value, config.quantity_max.value)
                        << "LimitOrder quantity " << e.quantity.value
                        << " above maximum " << config.quantity_max.value;
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    EXPECT_GE(e.quantity.value, config.quantity_min.value)
                        << "MarketOrder quantity " << e.quantity.value
                        << " below minimum " << config.quantity_min.value;
                    EXPECT_LE(e.quantity.value, config.quantity_max.value)
                        << "MarketOrder quantity " << e.quantity.value
                        << " above maximum " << config.quantity_max.value;
                }
            },
            event);
    }
}

// Verify cancel never references a non-existent ID
TEST(WorkloadGeneratorTest, CancelNeverReferencesNonExistentId) {
    WorkloadConfig config = make_default_config(999);
    // Use high cancel ratio to stress the cancel path
    config.cancel_ratio = 0.6;
    config.add_limit_ratio = 0.3;
    config.add_market_ratio = 0.1;

    WorkloadGenerator gen(config);
    auto events = gen.generate(10000);

    std::unordered_set<uint64_t> all_generated_ids;

    for (const auto& event : events) {
        std::visit(
            [&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, LimitOrder>) {
                    all_generated_ids.insert(e.id.value);
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    all_generated_ids.insert(e.id.value);
                } else if constexpr (std::is_same_v<T, CancelRequest>) {
                    EXPECT_TRUE(all_generated_ids.count(e.id.value) > 0)
                        << "CancelRequest references unknown OrderId "
                        << e.id.value;
                }
            },
            event);
    }
}
