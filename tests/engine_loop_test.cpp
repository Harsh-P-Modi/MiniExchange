#include "apps/exchange_server/engine_loop.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "core/EngineCommand.hpp"
#include "core/Events.hpp"
#include "core/TaggedCommand.hpp"
#include "core/Types.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/engine_api.hpp"
#include "orderbook/order_book.hpp"

namespace miniexchange {
namespace {

using exchange_server::command_order_id;
using exchange_server::dispatch_command;

// A test-only EngineAPI whose submit()/cancel() throw on demand — stands
// in for a genuine engine bug (invariant violation, bad_alloc, ...). The
// real MatchingEngine never throws for client input; this is how we
// exercise the R3 catch boundary without one.
class ThrowingEngine : public EngineAPI {
public:
    bool throw_on_submit = false;
    bool throw_on_cancel = false;
    int submit_calls = 0;
    int cancel_calls = 0;

    EngineResponse submit(const NewOrder& order) override {
        ++submit_calls;
        if (throw_on_submit) {
            throw std::runtime_error("boom in submit");
        }
        const OrderId id = std::visit([](const auto& o) { return o.id; }, order);
        return EngineResponse{EngineResult::Accepted, {}, Quantity{0}, id};
    }

    EngineResponse cancel(OrderId id) override {
        ++cancel_calls;
        if (throw_on_cancel) {
            throw std::runtime_error("boom in cancel");
        }
        return EngineResponse{EngineResult::Accepted, {}, Quantity{0}, id};
    }

    const OrderBook& book() const override { return book_; }

private:
    OrderBook book_{16};
};

TEST(EngineLoopTest, HappyPathForwardsToSubmitAndStampsOwner) {
    ThrowingEngine engine;
    TaggedCommand cmd{ClientId{9},
                      LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{5}}};

    EngineResponse resp = dispatch_command(engine, cmd);

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.order_id, OrderId{1});
    EXPECT_EQ(engine.submit_calls, 1);
    // owner stamped from the tagged command before dispatch (Phase 8).
    EXPECT_EQ(std::get<LimitOrder>(cmd.command).owner, ClientId{9});
}

TEST(EngineLoopTest, CancelIsRoutedToCancel) {
    ThrowingEngine engine;
    TaggedCommand cmd{ClientId{2}, CancelRequest{OrderId{77}}};

    EngineResponse resp = dispatch_command(engine, cmd);

    EXPECT_EQ(resp.status, EngineResult::Accepted);
    EXPECT_EQ(resp.order_id, OrderId{77});
    EXPECT_EQ(engine.cancel_calls, 1);
    EXPECT_EQ(engine.submit_calls, 0);
}

// R3 core: a throw from submit() is caught, does NOT propagate (the test
// process would crash if it did), and produces an InternalError response
// tagged to the originating order.
TEST(EngineLoopTest, SubmitThrowBecomesInternalErrorNotTerminate) {
    ThrowingEngine engine;
    engine.throw_on_submit = true;
    TaggedCommand cmd{
        ClientId{3},
        LimitOrder{OrderId{555}, Side::Sell, Price{100}, Quantity{5}}};

    EngineResponse resp = dispatch_command(engine, cmd);

    EXPECT_EQ(resp.status, EngineResult::InternalError);
    EXPECT_EQ(resp.order_id, OrderId{555});
    EXPECT_TRUE(resp.trades.empty());
}

TEST(EngineLoopTest, CancelThrowBecomesInternalError) {
    ThrowingEngine engine;
    engine.throw_on_cancel = true;
    TaggedCommand cmd{ClientId{4}, CancelRequest{OrderId{999}}};

    EngineResponse resp = dispatch_command(engine, cmd);

    EXPECT_EQ(resp.status, EngineResult::InternalError);
    EXPECT_EQ(resp.order_id, OrderId{999});
}

// After a throwing command, a subsequent non-throwing command from a
// different client is still processed correctly — the engine thread did
// not die, it just moved on (tasks.md T3).
TEST(EngineLoopTest, LoopSurvivesThrowAndProcessesNextCommand) {
    ThrowingEngine engine;

    engine.throw_on_submit = true;
    TaggedCommand bad{
        ClientId{1},
        LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{5}}};
    EngineResponse r_bad = dispatch_command(engine, bad);
    ASSERT_EQ(r_bad.status, EngineResult::InternalError);

    engine.throw_on_submit = false;
    TaggedCommand good{
        ClientId{2},
        LimitOrder{OrderId{2}, Side::Buy, Price{101}, Quantity{5}}};
    EngineResponse r_good = dispatch_command(engine, good);

    EXPECT_EQ(r_good.status, EngineResult::Accepted);
    EXPECT_EQ(r_good.order_id, OrderId{2});
    EXPECT_EQ(engine.submit_calls, 2);
}

// Sanity: dispatch_command against the real MatchingEngine still behaves
// like the old inline std::visit did (no behaviour change on the happy
// path — it just gained a catch that a correct engine never triggers).
TEST(EngineLoopTest, RealEngineUnchangedOnHappyPath) {
    MatchingEngine engine{NullEventSink::instance()};

    TaggedCommand add{
        ClientId{5},
        LimitOrder{OrderId{1}, Side::Buy, Price{100}, Quantity{10}}};
    EngineResponse r_add = dispatch_command(engine, add);
    EXPECT_EQ(r_add.status, EngineResult::Accepted);
    EXPECT_EQ(r_add.order_id, OrderId{1});
    EXPECT_EQ(engine.book().order_count(), 1u);

    TaggedCommand cxl{ClientId{5}, CancelRequest{OrderId{1}}};
    EngineResponse r_cxl = dispatch_command(engine, cxl);
    EXPECT_EQ(r_cxl.status, EngineResult::Accepted);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ===========================================================================
// Phase 11 T6 (R6) — WakeupCoalescer: at most one eventfd write per
// "drain the inbound queue until empty" cycle.
// ===========================================================================

using exchange_server::WakeupCoalescer;

// Simulate the engine loop's control flow over a scripted stream of
// inbound.try_pop() outcomes (true = a command was popped and a response
// pushed; false = queue empty) and count how many eventfd writes the
// coalescer would issue.
static int simulate_notifies(const std::vector<bool>& popped_stream) {
    WakeupCoalescer wakeup;
    int notifies = 0;
    for (bool popped : popped_stream) {
        if (!popped) {
            if (wakeup.should_notify_on_idle()) ++notifies;
            continue;  // (yield)
        }
        // ... dispatch + outbound.try_push ...
        wakeup.note_response();
    }
    // trailing flush (shutdown path)
    if (wakeup.should_notify_on_idle()) ++notifies;
    return notifies;
}

TEST(WakeupCoalescerTest, NoResponsesNoNotify) {
    EXPECT_EQ(simulate_notifies({false, false, false}), 0);
}

TEST(WakeupCoalescerTest, SingleResponseThenIdleNotifiesOnce) {
    // one command, then the queue reads empty -> exactly one wakeup,
    // issued immediately (no added latency under light load).
    EXPECT_EQ(simulate_notifies({true, false, false, false}), 1);
}

TEST(WakeupCoalescerTest, BurstOfManyResponsesCollapsesToOneNotify) {
    std::vector<bool> stream(100, true);  // 100 commands back-to-back
    stream.push_back(false);              // then idle
    EXPECT_EQ(simulate_notifies(stream), 1);
    // The pre-R6 behaviour would have been 100 eventfd writes.
}

TEST(WakeupCoalescerTest, OneNotifyPerBurst) {
    // burst, idle, burst, idle, burst, (trailing flush of last burst)
    std::vector<bool> stream = {true, true, true, false,
                                true, true, false,
                                true};
    EXPECT_EQ(simulate_notifies(stream), 3);
}

TEST(WakeupCoalescerTest, IdleWithNothingPendingIsCheap) {
    WakeupCoalescer w;
    EXPECT_FALSE(w.should_notify_on_idle());
    EXPECT_FALSE(w.pending());
    w.note_response();
    EXPECT_TRUE(w.pending());
    EXPECT_TRUE(w.should_notify_on_idle());
    EXPECT_FALSE(w.pending());               // cleared
    EXPECT_FALSE(w.should_notify_on_idle());  // no double-fire
}

// command_order_id extracts the id from every EngineCommand alternative.
TEST(EngineLoopTest, CommandOrderIdCoversAllAlternatives) {
    EXPECT_EQ(command_order_id(EngineCommand{LimitOrder{
                  OrderId{10}, Side::Buy, Price{1}, Quantity{1}}}),
              OrderId{10});
    EXPECT_EQ(command_order_id(EngineCommand{
                  MarketOrder{OrderId{11}, Side::Sell, Quantity{1}}}),
              OrderId{11});
    EXPECT_EQ(command_order_id(EngineCommand{CancelRequest{OrderId{12}}}),
              OrderId{12});
}

}  // namespace
}  // namespace miniexchange
