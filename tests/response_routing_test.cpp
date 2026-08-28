#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "core/EngineCommand.hpp"
#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/TaggedCommand.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

using namespace miniexchange;

// ============================================================
// Helper: dispatches an EngineCommand to the MatchingEngine,
// mirroring what the exchange_server's engine thread will do.
// ============================================================
static EngineResponse dispatch_command(MatchingEngine& engine,
                                       EngineCommand& command) {
    return std::visit(
        [&](auto& cmd) -> EngineResponse {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, LimitOrder>) {
                return engine.submit(NewOrder{cmd});
            } else if constexpr (std::is_same_v<T, MarketOrder>) {
                return engine.submit(NewOrder{cmd});
            } else {
                static_assert(std::is_same_v<T, CancelRequest>);
                return engine.cancel(cmd.id);
            }
        },
        command);
}

// ============================================================
// Test: Multiple clients submit orders through the SPSC queues,
// engine processes them, and each client receives only its own
// response — no cross-talk.
// ============================================================

TEST(ResponseRoutingTest, MultipleClientsGetOwnResponses) {
    SpscRingBuffer<TaggedCommand, 64> inbound;
    SpscRingBuffer<TaggedResponse, 64> outbound;
    MatchingEngine engine;

    ClientId client_a{1};
    ClientId client_b{2};

    // Client A submits a limit buy that rests (no opposite side yet)
    TaggedCommand cmd_a;
    cmd_a.client = client_a;
    cmd_a.command = LimitOrder{OrderId{100}, Side::Buy, Price{50}, Quantity{10}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd_a)));

    // Client B submits a limit sell that crosses A's buy
    TaggedCommand cmd_b;
    cmd_b.client = client_b;
    cmd_b.command =
        LimitOrder{OrderId{200}, Side::Sell, Price{50}, Quantity{10}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd_b)));

    // --- Engine thread simulation ---
    // Pop commands, process, push tagged responses onto outbound queue
    TaggedCommand popped;
    while (inbound.try_pop(popped)) {
        EngineResponse resp = dispatch_command(engine, popped.command);
        TaggedResponse tagged;
        tagged.client = popped.client;
        tagged.response = std::move(resp);
        // Spin-push per R8 (in practice the queue is large enough here)
        while (!outbound.try_push(std::move(tagged))) {
        }
    }

    // --- I/O thread simulation ---
    // Drain outbound and verify each response is tagged with the correct
    // ClientId (routing correctness).
    std::vector<TaggedResponse> responses;
    TaggedResponse resp;
    while (outbound.try_pop(resp)) {
        responses.push_back(std::move(resp));
    }

    ASSERT_EQ(responses.size(), 2u);

    // Client A's response (first command processed): rests, no match yet
    EXPECT_EQ(responses[0].client, client_a);
    EXPECT_EQ(responses[0].response.status, EngineResult::Accepted);
    EXPECT_TRUE(responses[0].response.trades.empty());
    EXPECT_EQ(responses[0].response.remaining_qty, Quantity{10});

    // Client B's response (second command): crosses A's resting buy
    EXPECT_EQ(responses[1].client, client_b);
    EXPECT_EQ(responses[1].response.status, EngineResult::Accepted);
    ASSERT_EQ(responses[1].response.trades.size(), 1u);
    EXPECT_EQ(responses[1].response.trades[0].price, Price{50});
    EXPECT_EQ(responses[1].response.trades[0].quantity, Quantity{10});
    EXPECT_EQ(responses[1].response.remaining_qty, Quantity{0});
}

// ============================================================
// Test: Three clients each submit a non-crossing order. Verify
// that FIFO order is preserved through the queue and each
// response is tagged with its own ClientId — no cross-talk.
// ============================================================

TEST(ResponseRoutingTest, CrossTalkVerification) {
    SpscRingBuffer<TaggedCommand, 64> inbound;
    SpscRingBuffer<TaggedResponse, 64> outbound;
    MatchingEngine engine;

    // Three clients submit non-crossing buy orders at different prices
    for (uint64_t i = 1; i <= 3; ++i) {
        TaggedCommand cmd;
        cmd.client = ClientId{i};
        cmd.command = LimitOrder{OrderId{i}, Side::Buy,
                                 Price{static_cast<int64_t>(100 + i)},
                                 Quantity{10}};
        ASSERT_TRUE(inbound.try_push(std::move(cmd)));
    }

    // Engine thread simulation: process all
    TaggedCommand popped;
    while (inbound.try_pop(popped)) {
        EngineResponse resp = dispatch_command(engine, popped.command);
        TaggedResponse tagged;
        tagged.client = popped.client;
        tagged.response = std::move(resp);
        while (!outbound.try_push(std::move(tagged))) {
        }
    }

    // Verify: each response is tagged with the correct ClientId, in FIFO
    // order, and no client sees another's response.
    TaggedResponse resp;
    uint64_t expected_client = 1;
    while (outbound.try_pop(resp)) {
        EXPECT_EQ(resp.client, ClientId{expected_client});
        EXPECT_EQ(resp.response.status, EngineResult::Accepted);
        EXPECT_TRUE(resp.response.trades.empty());  // non-crossing
        EXPECT_EQ(resp.response.remaining_qty, Quantity{10});
        ++expected_client;
    }
    EXPECT_EQ(expected_client, 4u);  // processed all 3
}

// ============================================================
// Test: Mixed command types (limit, market, cancel) from
// different clients. Verifies the dispatch_command visitor
// routes to submit/cancel correctly and each tagged response
// goes to the right client.
// ============================================================

TEST(ResponseRoutingTest, MixedCommandTypesRoutedCorrectly) {
    SpscRingBuffer<TaggedCommand, 64> inbound;
    SpscRingBuffer<TaggedResponse, 64> outbound;
    MatchingEngine engine;

    ClientId client_a{10};
    ClientId client_b{20};
    ClientId client_c{30};

    // Client A: limit buy rests
    TaggedCommand cmd_a;
    cmd_a.client = client_a;
    cmd_a.command = LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{20}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd_a)));

    // Client B: cancel A's order (valid cancel by a different client —
    // the engine doesn't enforce ownership, just OrderId existence)
    TaggedCommand cmd_b;
    cmd_b.client = client_b;
    cmd_b.command = CancelRequest{OrderId{1}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd_b)));

    // Client C: market sell — no resting orders left after the cancel
    TaggedCommand cmd_c;
    cmd_c.client = client_c;
    cmd_c.command = MarketOrder{OrderId{2}, Side::Sell, Quantity{5}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd_c)));

    // Engine thread processes all
    TaggedCommand popped;
    while (inbound.try_pop(popped)) {
        EngineResponse resp = dispatch_command(engine, popped.command);
        TaggedResponse tagged;
        tagged.client = popped.client;
        tagged.response = std::move(resp);
        while (!outbound.try_push(std::move(tagged))) {
        }
    }

    // Drain and verify
    std::vector<TaggedResponse> responses;
    TaggedResponse resp;
    while (outbound.try_pop(resp)) {
        responses.push_back(std::move(resp));
    }

    ASSERT_EQ(responses.size(), 3u);

    // Client A: limit buy accepted, rests
    EXPECT_EQ(responses[0].client, client_a);
    EXPECT_EQ(responses[0].response.status, EngineResult::Accepted);

    // Client B: cancel succeeds
    EXPECT_EQ(responses[1].client, client_b);
    EXPECT_EQ(responses[1].response.status, EngineResult::Accepted);

    // Client C: market sell — no liquidity, accepted but unfilled
    EXPECT_EQ(responses[2].client, client_c);
    EXPECT_EQ(responses[2].response.status, EngineResult::Accepted);
    EXPECT_TRUE(responses[2].response.trades.empty());
}

// ============================================================
// Test: Disconnected-client scenario — verifies that responses
// for a "disconnected" client can simply be dropped at the
// routing stage without affecting other clients' responses.
// ============================================================

TEST(ResponseRoutingTest, DisconnectedClientResponseDropped) {
    SpscRingBuffer<TaggedCommand, 64> inbound;
    SpscRingBuffer<TaggedResponse, 64> outbound;
    MatchingEngine engine;

    ClientId active_client{1};
    ClientId disconnected_client{99};

    // Both clients submit orders
    TaggedCommand cmd1;
    cmd1.client = active_client;
    cmd1.command =
        LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd1)));

    TaggedCommand cmd2;
    cmd2.client = disconnected_client;
    cmd2.command =
        LimitOrder{OrderId{2}, Side::Sell, Price{200}, Quantity{10}};
    ASSERT_TRUE(inbound.try_push(std::move(cmd2)));

    // Engine processes both (engine has no concept of "connected")
    TaggedCommand popped;
    while (inbound.try_pop(popped)) {
        EngineResponse resp = dispatch_command(engine, popped.command);
        TaggedResponse tagged;
        tagged.client = popped.client;
        tagged.response = std::move(resp);
        while (!outbound.try_push(std::move(tagged))) {
        }
    }

    // I/O thread simulation: drain outbound, skip disconnected clients
    // (simulates Task 5.5's "disconnected client's response dropped" logic)
    std::vector<TaggedResponse> delivered;
    TaggedResponse resp;
    while (outbound.try_pop(resp)) {
        if (resp.client == disconnected_client) {
            continue;  // dropped — client is gone
        }
        delivered.push_back(std::move(resp));
    }

    // Only the active client's response was delivered
    ASSERT_EQ(delivered.size(), 1u);
    EXPECT_EQ(delivered[0].client, active_client);
    EXPECT_EQ(delivered[0].response.status, EngineResult::Accepted);
}
