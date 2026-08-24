#include "interfaces/engine_api.hpp"
#include "interfaces/event_sink.hpp"

#include <gtest/gtest.h>
#include <type_traits>

using namespace miniexchange;

// ---------------------------------------------------------------------------
// Compile-time structural guarantees
// ---------------------------------------------------------------------------

// NullEventSink IS-A EventSink
static_assert(std::is_base_of_v<EventSink, NullEventSink>,
              "NullEventSink must derive from EventSink");

// EngineAPI is abstract: cannot be instantiated directly.
// std::is_abstract_v is true when a class has at least one pure virtual
// method that is not overridden.
static_assert(std::is_abstract_v<EngineAPI>,
              "EngineAPI must be abstract (has pure virtual methods)");

// EventSink itself is abstract.
static_assert(std::is_abstract_v<EventSink>,
              "EventSink must be abstract (has pure virtual methods)");

// NullEventSink is concrete (all pure virtuals overridden).
static_assert(!std::is_abstract_v<NullEventSink>,
              "NullEventSink must be concrete (all pure virtuals overridden)");

// ---------------------------------------------------------------------------
// NullEventSink — no-op behaviour and singleton identity
// ---------------------------------------------------------------------------

TEST(NullEventSinkTest, SingletonReturnsSamePointer) {
    NullEventSink* a = NullEventSink::instance();
    NullEventSink* b = NullEventSink::instance();
    EXPECT_EQ(a, b) << "instance() must always return the same object";
}

TEST(NullEventSinkTest, SingletonIsNotNull) {
    EXPECT_NE(NullEventSink::instance(), nullptr);
}

TEST(NullEventSinkTest, OnTradeDoesNotCrash) {
    Trade dummy_trade{
        TradeSequence{1},
        OrderId{10},
        OrderId{20},
        Price{10000},
        Quantity{50}
    };
    // Must not crash; return value is void, so the call itself is the test.
    NullEventSink::instance()->on_trade(dummy_trade);
}

TEST(NullEventSinkTest, OnOrderAcceptedDoesNotCrash) {
    OrderAccepted dummy_event{OrderId{42}, Side::Buy, Quantity{100}};
    NullEventSink::instance()->on_order_accepted(dummy_event);
}

TEST(NullEventSinkTest, OnOrderCancelledDoesNotCrash) {
    OrderCancelled dummy_event{OrderId{99}, Quantity{30}};
    NullEventSink::instance()->on_order_cancelled(dummy_event);
}

// Call all three through the base EventSink* interface — confirms the
// virtual dispatch path works correctly (not just through the concrete type).
TEST(NullEventSinkTest, PolymorphicDispatchAllMethods) {
    EventSink* sink = NullEventSink::instance();

    Trade t{TradeSequence{5}, OrderId{1}, OrderId{2}, Price{500}, Quantity{10}};
    sink->on_trade(t);

    OrderAccepted oa{OrderId{1}, Side::Sell, Quantity{25}};
    sink->on_order_accepted(oa);

    OrderCancelled oc{OrderId{2}, Quantity{5}};
    sink->on_order_cancelled(oc);
    // If we reach here without a crash, polymorphic dispatch is working.
}

// ---------------------------------------------------------------------------
// RecordingEventSink — verify that a concrete non-trivial EventSink
// subtype can be written and wired through the interface.
// This also serves as a forward-compatibility test: later tasks will
// use RecordingEventSink to verify R16-R20.
// ---------------------------------------------------------------------------

class RecordingEventSink final : public EventSink {
public:
    int trade_count{0};
    int accepted_count{0};
    int cancelled_count{0};

    void on_trade(const Trade& /*trade*/) override { ++trade_count; }
    void on_order_accepted(const OrderAccepted& /*event*/) override { ++accepted_count; }
    void on_order_cancelled(const OrderCancelled& /*event*/) override { ++cancelled_count; }
};

TEST(RecordingEventSinkTest, CountsEachEventType) {
    RecordingEventSink sink;
    EventSink* base = &sink;

    Trade t{TradeSequence{1}, OrderId{10}, OrderId{20}, Price{100}, Quantity{5}};
    base->on_trade(t);
    base->on_trade(t);

    OrderAccepted oa{OrderId{10}, Side::Buy, Quantity{5}};
    base->on_order_accepted(oa);

    OrderCancelled oc{OrderId{20}, Quantity{3}};
    base->on_order_cancelled(oc);
    base->on_order_cancelled(oc);
    base->on_order_cancelled(oc);

    EXPECT_EQ(sink.trade_count, 2);
    EXPECT_EQ(sink.accepted_count, 1);
    EXPECT_EQ(sink.cancelled_count, 3);
}
