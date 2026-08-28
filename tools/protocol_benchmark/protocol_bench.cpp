// tools/protocol_benchmark/protocol_bench.cpp — Tasks 13+14: Binary vs JSON
// encode/decode latency benchmark with payload size and allocation attribution.
//
// For each of the 6 message types, measures:
//   - Encode latency (binary vs JSON)
//   - Decode latency (binary vs JSON)
//   - Payload size in bytes (reported as custom counter)
//   - Allocation count per JSON operation (reported as custom counter)
//
// Run: ./build/protocol_benchmark --benchmark_repetitions=10
// NOT registered with ctest — run manually.

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/JsonCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"

namespace {

// --- Allocation counter for JSON attribution ---
// Thread-local so benchmarks don't interfere with each other.
thread_local std::size_t g_alloc_count = 0;
thread_local bool g_counting = false;

struct AllocGuard {
    AllocGuard() { g_alloc_count = 0; g_counting = true; }
    ~AllocGuard() { g_counting = false; }
    std::size_t count() const { return g_alloc_count; }
};

}  // namespace

// Override global operator new to count allocations when counting is enabled.
void* operator new(std::size_t size) {
    if (g_counting) ++g_alloc_count;
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    if (p) std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    if (p) std::free(p);
}

namespace miniexchange::binary_protocol {
namespace {

// --- Representative message instances ---

LimitOrderAddMsg make_limit_order() {
    LimitOrderAddMsg msg;
    msg.type = MessageType::LimitOrderAdd;
    msg.side = 0;
    msg.client_id = ClientId{42};
    msg.order_id = OrderId{1001};
    msg.price = Price{9950};
    msg.quantity = Quantity{500};
    return msg;
}

MarketOrderAddMsg make_market_order() {
    MarketOrderAddMsg msg;
    msg.type = MessageType::MarketOrderAdd;
    msg.side = 1;
    msg.client_id = ClientId{7};
    msg.order_id = OrderId{2002};
    msg.quantity = Quantity{100};
    return msg;
}

CancelMsg make_cancel() {
    CancelMsg msg;
    msg.type = MessageType::Cancel;
    msg.padding = 0;
    msg.client_id = ClientId{42};
    msg.order_id = OrderId{1001};
    return msg;
}

AckMsg make_ack() {
    AckMsg msg;
    msg.type = MessageType::Ack;
    msg.padding = 0;
    msg.order_id = OrderId{1001};
    msg.remaining_qty = Quantity{250};
    return msg;
}

RejectMsg make_reject() {
    RejectMsg msg;
    msg.type = MessageType::Reject;
    msg.reason_code = 1;
    msg.order_id = OrderId{1001};
    return msg;
}

TradeNotificationMsg make_trade_notification() {
    TradeNotificationMsg msg;
    msg.type = MessageType::TradeNotification;
    msg.padding = 0;
    msg.buy_order_id = OrderId{1001};
    msg.sell_order_id = OrderId{2002};
    msg.price = Price{9950};
    msg.quantity = Quantity{100};
    msg.trade_sequence = TradeSequence{42};
    return msg;
}

// ============================================================
// Binary encode benchmarks
// ============================================================

static void BM_BinaryEncode_LimitOrder(benchmark::State& state) {
    auto msg = make_limit_order();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t bytes = 0;
    for (auto _ : state) {
        bytes = encode(msg, buf);
        benchmark::DoNotOptimize(buf.data());
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
}
BENCHMARK(BM_BinaryEncode_LimitOrder);

static void BM_BinaryEncode_MarketOrder(benchmark::State& state) {
    auto msg = make_market_order();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t bytes = 0;
    for (auto _ : state) {
        bytes = encode(msg, buf);
        benchmark::DoNotOptimize(buf.data());
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
}
BENCHMARK(BM_BinaryEncode_MarketOrder);

static void BM_BinaryEncode_Cancel(benchmark::State& state) {
    auto msg = make_cancel();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t bytes = 0;
    for (auto _ : state) {
        bytes = encode(msg, buf);
        benchmark::DoNotOptimize(buf.data());
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
}
BENCHMARK(BM_BinaryEncode_Cancel);

static void BM_BinaryEncode_Ack(benchmark::State& state) {
    auto msg = make_ack();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t bytes = 0;
    for (auto _ : state) {
        bytes = encode(msg, buf);
        benchmark::DoNotOptimize(buf.data());
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
}
BENCHMARK(BM_BinaryEncode_Ack);

static void BM_BinaryEncode_Reject(benchmark::State& state) {
    auto msg = make_reject();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t bytes = 0;
    for (auto _ : state) {
        bytes = encode(msg, buf);
        benchmark::DoNotOptimize(buf.data());
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
}
BENCHMARK(BM_BinaryEncode_Reject);

static void BM_BinaryEncode_TradeNotification(benchmark::State& state) {
    auto msg = make_trade_notification();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t bytes = 0;
    for (auto _ : state) {
        bytes = encode(msg, buf);
        benchmark::DoNotOptimize(buf.data());
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
}
BENCHMARK(BM_BinaryEncode_TradeNotification);

// ============================================================
// Binary decode benchmarks
// ============================================================

static void BM_BinaryDecode_LimitOrder(benchmark::State& state) {
    auto msg = make_limit_order();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    auto span = std::span<const std::byte>(buf.data(), written);
    for (auto _ : state) {
        auto result = decode(span);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_BinaryDecode_LimitOrder);

static void BM_BinaryDecode_MarketOrder(benchmark::State& state) {
    auto msg = make_market_order();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    auto span = std::span<const std::byte>(buf.data(), written);
    for (auto _ : state) {
        auto result = decode(span);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_BinaryDecode_MarketOrder);

static void BM_BinaryDecode_Cancel(benchmark::State& state) {
    auto msg = make_cancel();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    auto span = std::span<const std::byte>(buf.data(), written);
    for (auto _ : state) {
        auto result = decode(span);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_BinaryDecode_Cancel);

static void BM_BinaryDecode_Ack(benchmark::State& state) {
    auto msg = make_ack();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    auto span = std::span<const std::byte>(buf.data(), written);
    for (auto _ : state) {
        auto result = decode(span);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_BinaryDecode_Ack);

static void BM_BinaryDecode_Reject(benchmark::State& state) {
    auto msg = make_reject();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    auto span = std::span<const std::byte>(buf.data(), written);
    for (auto _ : state) {
        auto result = decode(span);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_BinaryDecode_Reject);

static void BM_BinaryDecode_TradeNotification(benchmark::State& state) {
    auto msg = make_trade_notification();
    std::array<std::byte, kMaxMessageWireSize> buf{};
    std::size_t written = encode(msg, buf);
    auto span = std::span<const std::byte>(buf.data(), written);
    for (auto _ : state) {
        auto result = decode(span);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_BinaryDecode_TradeNotification);

// ============================================================
// JSON encode benchmarks (with allocation + byte counters)
// ============================================================

static void BM_JsonEncode_LimitOrder(benchmark::State& state) {
    auto msg = make_limit_order();
    std::size_t bytes = 0;
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        nlohmann::json j = msg;
        std::string s = j.dump();
        benchmark::DoNotOptimize(s.data());
        bytes = s.size();
        allocs = guard.count();
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonEncode_LimitOrder);

static void BM_JsonEncode_MarketOrder(benchmark::State& state) {
    auto msg = make_market_order();
    std::size_t bytes = 0;
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        nlohmann::json j = msg;
        std::string s = j.dump();
        benchmark::DoNotOptimize(s.data());
        bytes = s.size();
        allocs = guard.count();
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonEncode_MarketOrder);

static void BM_JsonEncode_Cancel(benchmark::State& state) {
    auto msg = make_cancel();
    std::size_t bytes = 0;
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        nlohmann::json j = msg;
        std::string s = j.dump();
        benchmark::DoNotOptimize(s.data());
        bytes = s.size();
        allocs = guard.count();
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonEncode_Cancel);

static void BM_JsonEncode_Ack(benchmark::State& state) {
    auto msg = make_ack();
    std::size_t bytes = 0;
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        nlohmann::json j = msg;
        std::string s = j.dump();
        benchmark::DoNotOptimize(s.data());
        bytes = s.size();
        allocs = guard.count();
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonEncode_Ack);

static void BM_JsonEncode_Reject(benchmark::State& state) {
    auto msg = make_reject();
    std::size_t bytes = 0;
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        nlohmann::json j = msg;
        std::string s = j.dump();
        benchmark::DoNotOptimize(s.data());
        bytes = s.size();
        allocs = guard.count();
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonEncode_Reject);

static void BM_JsonEncode_TradeNotification(benchmark::State& state) {
    auto msg = make_trade_notification();
    std::size_t bytes = 0;
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        nlohmann::json j = msg;
        std::string s = j.dump();
        benchmark::DoNotOptimize(s.data());
        bytes = s.size();
        allocs = guard.count();
    }
    state.counters["Bytes"] = static_cast<double>(bytes);
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonEncode_TradeNotification);

// ============================================================
// JSON decode benchmarks (with allocation counter)
// ============================================================

static void BM_JsonDecode_LimitOrder(benchmark::State& state) {
    auto msg = make_limit_order();
    nlohmann::json j = msg;
    std::string s = j.dump();
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        auto parsed = nlohmann::json::parse(s);
        auto decoded = parsed.get<LimitOrderAddMsg>();
        benchmark::DoNotOptimize(decoded);
        allocs = guard.count();
    }
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonDecode_LimitOrder);

static void BM_JsonDecode_MarketOrder(benchmark::State& state) {
    auto msg = make_market_order();
    nlohmann::json j = msg;
    std::string s = j.dump();
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        auto parsed = nlohmann::json::parse(s);
        auto decoded = parsed.get<MarketOrderAddMsg>();
        benchmark::DoNotOptimize(decoded);
        allocs = guard.count();
    }
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonDecode_MarketOrder);

static void BM_JsonDecode_Cancel(benchmark::State& state) {
    auto msg = make_cancel();
    nlohmann::json j = msg;
    std::string s = j.dump();
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        auto parsed = nlohmann::json::parse(s);
        auto decoded = parsed.get<CancelMsg>();
        benchmark::DoNotOptimize(decoded);
        allocs = guard.count();
    }
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonDecode_Cancel);

static void BM_JsonDecode_Ack(benchmark::State& state) {
    auto msg = make_ack();
    nlohmann::json j = msg;
    std::string s = j.dump();
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        auto parsed = nlohmann::json::parse(s);
        auto decoded = parsed.get<AckMsg>();
        benchmark::DoNotOptimize(decoded);
        allocs = guard.count();
    }
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonDecode_Ack);

static void BM_JsonDecode_Reject(benchmark::State& state) {
    auto msg = make_reject();
    nlohmann::json j = msg;
    std::string s = j.dump();
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        auto parsed = nlohmann::json::parse(s);
        auto decoded = parsed.get<RejectMsg>();
        benchmark::DoNotOptimize(decoded);
        allocs = guard.count();
    }
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonDecode_Reject);

static void BM_JsonDecode_TradeNotification(benchmark::State& state) {
    auto msg = make_trade_notification();
    nlohmann::json j = msg;
    std::string s = j.dump();
    std::size_t allocs = 0;
    for (auto _ : state) {
        AllocGuard guard;
        auto parsed = nlohmann::json::parse(s);
        auto decoded = parsed.get<TradeNotificationMsg>();
        benchmark::DoNotOptimize(decoded);
        allocs = guard.count();
    }
    state.counters["Allocations"] = static_cast<double>(allocs);
}
BENCHMARK(BM_JsonDecode_TradeNotification);

}  // namespace
}  // namespace miniexchange::binary_protocol

BENCHMARK_MAIN();
