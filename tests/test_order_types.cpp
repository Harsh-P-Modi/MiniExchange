#include <gtest/gtest.h>
#include "../core/Order.hpp"
#include "../core/NewOrder.hpp"
#include <variant>
#include <type_traits>

using namespace miniexchange;

// Test that Order struct can be constructed and fields accessed
TEST(OrderTypesTest, OrderStructBasics) {
    Order order{
        OrderId{1},
        Side::Buy,
        Price{10000},
        Quantity{50},
        Sequence{0}
    };

    EXPECT_EQ(order.id.value, 1u);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.price.value, 10000);
    EXPECT_EQ(order.quantity.value, 50u);
    EXPECT_EQ(order.sequence.value, 0u);
    EXPECT_EQ(order.prev, nullptr);
    EXPECT_EQ(order.next, nullptr);
    EXPECT_EQ(order.level, nullptr);
}

// Test that LimitOrder struct works correctly
TEST(OrderTypesTest, LimitOrderBasics) {
    LimitOrder limit{OrderId{42}, Side::Sell, Price{10020}, Quantity{30}};

    EXPECT_EQ(limit.id.value, 42u);
    EXPECT_EQ(limit.side, Side::Sell);
    EXPECT_EQ(limit.price.value, 10020);
    EXPECT_EQ(limit.quantity.value, 30u);
}

// Test that MarketOrder struct works correctly
TEST(OrderTypesTest, MarketOrderBasics) {
    MarketOrder market{OrderId{99}, Side::Buy, Quantity{100}};

    EXPECT_EQ(market.id.value, 99u);
    EXPECT_EQ(market.side, Side::Buy);
    EXPECT_EQ(market.quantity.value, 100u);
    // Critically: MarketOrder has no price member — enforced structurally
}

// Test that std::holds_alternative correctly distinguishes variant alternatives
TEST(OrderTypesTest, NewOrderVariantDiscrimination) {
    // Create a LimitOrder wrapped in NewOrder variant
    NewOrder limit_order = LimitOrder{OrderId{1}, Side::Buy, Price{10000}, Quantity{50}};
    EXPECT_TRUE(std::holds_alternative<LimitOrder>(limit_order));
    EXPECT_FALSE(std::holds_alternative<MarketOrder>(limit_order));

    // Create a MarketOrder wrapped in NewOrder variant
    NewOrder market_order = MarketOrder{OrderId{2}, Side::Sell, Quantity{30}};
    EXPECT_TRUE(std::holds_alternative<MarketOrder>(market_order));
    EXPECT_FALSE(std::holds_alternative<LimitOrder>(market_order));

    // Access the held values via std::get
    const auto& limit = std::get<LimitOrder>(limit_order);
    EXPECT_EQ(limit.id.value, 1u);
    EXPECT_EQ(limit.price.value, 10000);

    const auto& market = std::get<MarketOrder>(market_order);
    EXPECT_EQ(market.id.value, 2u);
    EXPECT_EQ(market.quantity.value, 30u);
}

// Compile-time verification that MarketOrder has no price member.
// This test uses C++20 concepts/requires expression to verify at compile
// time that attempting to access MarketOrder::price is ill-formed.
// Decision: using requires expression (C++20) over SFINAE or manual
// inspection because it's the most direct "this expression is invalid"
// check available in C++20, which is our locked standard.
//
// Note: The test is written as a compile-time check using a template
// helper to avoid confusing compiler error messages.
template <typename T>
concept HasPriceMember = requires(T t) {
    { t.price } -> std::convertible_to<Price>;
};

TEST(OrderTypesTest, MarketOrderHasNoPriceMember) {
    // This static_assert will fail to compile if MarketOrder gains a
    // price member — exactly what we want as a structural guarantee.
    static_assert(!HasPriceMember<MarketOrder>,
                  "MarketOrder must not have a price member — this is a "
                  "structural guarantee per requirements.md R11");

    // Conversely, confirm LimitOrder DOES have a price member (sanity check)
    static_assert(HasPriceMember<LimitOrder>,
                  "LimitOrder must have a price member");

    // This test body is intentionally empty — the compile-time checks
    // above are the actual test; if this file compiles, the test passes.
}
