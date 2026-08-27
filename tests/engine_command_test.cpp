#include "core/EngineCommand.hpp"

#include <gtest/gtest.h>
#include <variant>

namespace miniexchange {
namespace {

TEST(EngineCommandTest, HoldsLimitOrder) {
    LimitOrder limit{OrderId{1}, Side::Buy, Price{100}, Quantity{50}};
    EngineCommand cmd = limit;

    EXPECT_TRUE(std::holds_alternative<LimitOrder>(cmd));
    EXPECT_FALSE(std::holds_alternative<MarketOrder>(cmd));
    EXPECT_FALSE(std::holds_alternative<CancelRequest>(cmd));

    const auto& extracted = std::get<LimitOrder>(cmd);
    EXPECT_EQ(extracted.id, OrderId{1});
    EXPECT_EQ(extracted.side, Side::Buy);
    EXPECT_EQ(extracted.price, Price{100});
    EXPECT_EQ(extracted.quantity, Quantity{50});
}

TEST(EngineCommandTest, HoldsMarketOrder) {
    MarketOrder market{OrderId{2}, Side::Sell, Quantity{75}};
    EngineCommand cmd = market;

    EXPECT_FALSE(std::holds_alternative<LimitOrder>(cmd));
    EXPECT_TRUE(std::holds_alternative<MarketOrder>(cmd));
    EXPECT_FALSE(std::holds_alternative<CancelRequest>(cmd));

    const auto& extracted = std::get<MarketOrder>(cmd);
    EXPECT_EQ(extracted.id, OrderId{2});
    EXPECT_EQ(extracted.side, Side::Sell);
    EXPECT_EQ(extracted.quantity, Quantity{75});
}

TEST(EngineCommandTest, HoldsCancelRequest) {
    CancelRequest cancel{OrderId{3}};
    EngineCommand cmd = cancel;

    EXPECT_FALSE(std::holds_alternative<LimitOrder>(cmd));
    EXPECT_FALSE(std::holds_alternative<MarketOrder>(cmd));
    EXPECT_TRUE(std::holds_alternative<CancelRequest>(cmd));

    const auto& extracted = std::get<CancelRequest>(cmd);
    EXPECT_EQ(extracted.id, OrderId{3});
}

TEST(EngineCommandTest, VisitDispatchesCorrectly) {
    // Verifies that std::visit can distinguish all three alternatives,
    // which is the actual usage pattern in Phase 5's engine-thread loop.
    EngineCommand commands[] = {
        LimitOrder{OrderId{10}, Side::Buy, Price{200}, Quantity{5}},
        MarketOrder{OrderId{11}, Side::Sell, Quantity{10}},
        CancelRequest{OrderId{12}},
    };

    int limit_count = 0;
    int market_count = 0;
    int cancel_count = 0;

    for (const auto& cmd : commands) {
        std::visit(
            [&](const auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, LimitOrder>) {
                    ++limit_count;
                } else if constexpr (std::is_same_v<T, MarketOrder>) {
                    ++market_count;
                } else if constexpr (std::is_same_v<T, CancelRequest>) {
                    ++cancel_count;
                }
            },
            cmd);
    }

    EXPECT_EQ(limit_count, 1);
    EXPECT_EQ(market_count, 1);
    EXPECT_EQ(cancel_count, 1);
}

}  // namespace
}  // namespace miniexchange
