// apps/benchmark/tcp_roundtrip_bench.cpp — TCP round-trip latency benchmark.
//
// Measures the full end-to-end path:
//   client send → I/O thread parse → inbound queue → engine match →
//   outbound queue → eventfd → I/O thread render → frame → client recv
//
// This is a standalone executable (not part of the Google Benchmark harness)
// because it requires real sockets, epoll, eventfd, and two threads — all
// Linux-only. It composes the full exchange_server pipeline internally
// (same wiring as apps/exchange_server/main.cpp) and connects a local
// client socket for measurement.
//
// Usage:
//   ./build/tcp_roundtrip_bench [port] [iterations]
//   Default: port=0 (OS-assigned ephemeral), iterations=10000
//
// Recommended invocation with CPU pinning (per R7):
//   taskset -c 0,1 ./build/tcp_roundtrip_bench 0 10000

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <variant>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "adapters/tcp/framing.hpp"
#include "adapters/tcp/tcp_server.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "adapters/text_protocol/text_protocol_renderer.hpp"
#include "apps/benchmark/latency_recorder.hpp"
#include "core/EngineCommand.hpp"
#include "core/TaggedCommand.hpp"
#include "engine/matching_engine.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

namespace {

using namespace miniexchange;
using namespace miniexchange::tcp;
using namespace miniexchange::text_protocol;
using namespace miniexchange::benchmark;

// --- Server-side composition (mirrors exchange_server/main.cpp) ---

struct ServerContext {
    MatchingEngine engine;
    SpscRingBuffer<TaggedCommand, 4096> inbound;
    SpscRingBuffer<TaggedResponse, 65536> outbound;
    TcpServer server;
    int eventfd_fd = -1;
    std::atomic<bool> shutdown{false};

    explicit ServerContext(uint16_t port)
        : server(port) {
        eventfd_fd = ::eventfd(0, EFD_NONBLOCK);
        if (eventfd_fd < 0) {
            std::fprintf(stderr, "eventfd() failed: %s\n",
                         std::strerror(errno));
            std::exit(1);
        }
    }

    ~ServerContext() {
        if (eventfd_fd >= 0) {
            ::close(eventfd_fd);
        }
    }

    ServerContext(const ServerContext&) = delete;
    ServerContext& operator=(const ServerContext&) = delete;
};

void wire_server(ServerContext& ctx) {
    ctx.server.set_frame_handler(
        [&ctx](ClientId client_id, std::string_view payload) {
            auto result = parse(payload);
            std::visit(
                [&](auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, ParseError>) {
                        auto err_msg = render_error(val.message);
                        ctx.server.send_to_client(client_id,
                                                  frame_message(err_msg));
                    } else {
                        TaggedCommand cmd{client_id, val};
                        ctx.inbound.try_push(std::move(cmd));
                    }
                },
                result);
        });

    ctx.server.set_eventfd(ctx.eventfd_fd);

    ctx.server.set_response_drain_handler(
        [&ctx]() {
            TaggedResponse resp{};
            while (ctx.outbound.try_pop(resp)) {
                auto rendered = render(resp.response);
                ctx.server.send_to_client(resp.client, frame_message(rendered));
            }
        });
}

void engine_thread_loop(ServerContext& ctx) {
    while (!ctx.shutdown.load(std::memory_order_relaxed)) {
        TaggedCommand cmd{};
        if (!ctx.inbound.try_pop(cmd)) {
            std::this_thread::yield();
            continue;
        }

        EngineResponse resp = std::visit(
            [&ctx](auto& command) -> EngineResponse {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, LimitOrder> ||
                              std::is_same_v<T, MarketOrder>) {
                    return ctx.engine.submit(command);
                } else {
                    static_assert(std::is_same_v<T, CancelRequest>);
                    return ctx.engine.cancel(command.id);
                }
            },
            cmd.command);

        TaggedResponse tagged{cmd.client, std::move(resp)};
        while (ctx.outbound.size() >= ctx.outbound.capacity()) {
            std::this_thread::yield();
        }
        ctx.outbound.try_push(std::move(tagged));

        uint64_t val = 1;
        [[maybe_unused]] auto _ = ::write(ctx.eventfd_fd, &val, sizeof(val));
    }
}

// --- Client-side: connect and send/receive framed messages ---

int connect_to_server(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "socket() failed: %s\n", std::strerror(errno));
        std::exit(1);
    }

    // Set TCP_NODELAY on client side too (minimize send delay)
    int flag = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        std::fprintf(stderr, "connect() failed: %s\n", std::strerror(errno));
        ::close(fd);
        std::exit(1);
    }

    return fd;
}

// Send a length-prefixed frame (blocking write — fine for a benchmark client).
void send_frame(int fd, std::string_view payload) {
    std::string framed = frame_message(payload);
    const char* ptr = framed.data();
    std::size_t remaining = framed.size();
    while (remaining > 0) {
        auto n = ::write(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "write() failed: %s\n", std::strerror(errno));
            std::exit(1);
        }
        ptr += n;
        remaining -= static_cast<std::size_t>(n);
    }
}

// Receive one length-prefixed frame (blocking read — fine for a benchmark client).
// Returns the payload (without the 4-byte length prefix).
std::string recv_frame(int fd) {
    // Read 4-byte length prefix
    uint8_t len_buf[4];
    std::size_t got = 0;
    while (got < 4) {
        auto n = ::read(fd, len_buf + got, 4 - got);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            std::fprintf(stderr, "read() length prefix failed (n=%zd): %s\n",
                         n, std::strerror(errno));
            std::exit(1);
        }
        got += static_cast<std::size_t>(n);
    }

    uint32_t payload_len = (static_cast<uint32_t>(len_buf[0]) << 24) |
                           (static_cast<uint32_t>(len_buf[1]) << 16) |
                           (static_cast<uint32_t>(len_buf[2]) << 8) |
                           static_cast<uint32_t>(len_buf[3]);

    // Read payload
    std::string payload(payload_len, '\0');
    got = 0;
    while (got < payload_len) {
        auto n = ::read(fd, payload.data() + got, payload_len - got);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            std::fprintf(stderr, "read() payload failed (n=%zd): %s\n",
                         n, std::strerror(errno));
            std::exit(1);
        }
        got += static_cast<std::size_t>(n);
    }

    return payload;
}

void print_results(const char* label, const LatencyRecorder& recorder) {
    std::printf("  %-30s  avg=%8.1f  median=%8.1f  P99=%8.1f  max=%8.1f ns\n",
                label, recorder.avg_ns(), recorder.median_ns(),
                recorder.p99_ns(), recorder.max_ns());
}

}  // namespace

int main(int argc, char* argv[]) {
    // Parse arguments
    uint16_t port = 0;  // 0 = OS-assigned ephemeral port
    std::size_t iterations = 10000;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    if (argc >= 3) {
        iterations = static_cast<std::size_t>(std::atol(argv[2]));
    }

    std::printf("=== TCP Round-Trip Latency Benchmark ===\n");
    std::printf("Iterations: %zu\n\n", iterations);

    // --- Start server ---
    ServerContext ctx(port);
    wire_server(ctx);

    // Spawn I/O thread
    std::thread io_thread([&ctx]() { ctx.server.run(); });

    // Spawn engine thread
    std::thread eng_thread([&ctx]() { engine_thread_loop(ctx); });

    // Give the server a moment to bind and listen
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Get the actual port (in case we used 0 for ephemeral)
    uint16_t actual_port = ctx.server.port();
    std::printf("Server listening on port %u\n\n", actual_port);

    // --- Connect client ---
    int client_fd = connect_to_server(actual_port);

    // --- Warmup: a few round-trips to prime caches and JIT paths ---
    constexpr std::size_t kWarmup = 100;
    for (std::size_t i = 0; i < kWarmup; ++i) {
        std::string cmd = "ADD " + std::to_string(i + 1) + " BUY 100 10";
        send_frame(client_fd, cmd);
        recv_frame(client_fd);
    }

    // Cancel all warmup orders to clear the book
    for (std::size_t i = 0; i < kWarmup; ++i) {
        std::string cmd = "CANCEL " + std::to_string(i + 1);
        send_frame(client_fd, cmd);
        recv_frame(client_fd);
    }

    // --- Benchmark: non-crossing limit orders (pure round-trip, no match) ---
    LatencyRecorder rec_no_match;
    std::size_t order_id_base = kWarmup + 1;

    for (std::size_t i = 0; i < iterations; ++i) {
        std::string cmd = "ADD " + std::to_string(order_id_base + i) +
                          " BUY 100 10";

        auto start = std::chrono::steady_clock::now();
        send_frame(client_fd, cmd);
        recv_frame(client_fd);
        auto end = std::chrono::steady_clock::now();

        rec_no_match.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }

    // Cancel all orders to clear the book for the next test
    for (std::size_t i = 0; i < iterations; ++i) {
        std::string cmd = "CANCEL " + std::to_string(order_id_base + i);
        send_frame(client_fd, cmd);
        recv_frame(client_fd);
    }
    order_id_base += iterations;

    // --- Benchmark: cancel (immediate response, no match) ---
    LatencyRecorder rec_cancel;

    // First, add orders to cancel
    for (std::size_t i = 0; i < iterations; ++i) {
        std::string cmd = "ADD " + std::to_string(order_id_base + i) +
                          " BUY 100 10";
        send_frame(client_fd, cmd);
        recv_frame(client_fd);
    }

    // Now time the cancels
    for (std::size_t i = 0; i < iterations; ++i) {
        std::string cmd = "CANCEL " + std::to_string(order_id_base + i);

        auto start = std::chrono::steady_clock::now();
        send_frame(client_fd, cmd);
        recv_frame(client_fd);
        auto end = std::chrono::steady_clock::now();

        rec_cancel.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
    order_id_base += iterations;

    // --- Benchmark: single fill (aggressive order crosses one resting) ---
    LatencyRecorder rec_1fill;

    for (std::size_t i = 0; i < iterations; ++i) {
        // Place a resting sell order (untimed)
        std::string resting = "ADD " + std::to_string(order_id_base + i * 2) +
                              " SELL 100 10";
        send_frame(client_fd, resting);
        recv_frame(client_fd);

        // Time the aggressive buy that crosses it
        std::string aggressive =
            "ADD " + std::to_string(order_id_base + i * 2 + 1) +
            " BUY 100 10";

        auto start = std::chrono::steady_clock::now();
        send_frame(client_fd, aggressive);
        recv_frame(client_fd);
        auto end = std::chrono::steady_clock::now();

        rec_1fill.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start));
    }
    order_id_base += iterations * 2;

    // --- Print results ---
    std::printf("Results (%zu iterations each):\n", iterations);
    print_results("ADD (no match, round-trip)", rec_no_match);
    print_results("CANCEL (round-trip)", rec_cancel);
    print_results("ADD (1 fill, round-trip)", rec_1fill);

    std::printf("\nPhase 2 engine-internal reference (median):\n");
    std::printf("  ADD (no match): ~900 ns\n");
    std::printf("  CANCEL:         ~300 ns\n");
    std::printf("  ADD (1 fill):   ~600 ns\n");
    std::printf("\nTCP overhead = round-trip median - engine-internal median\n");

    // --- Cleanup ---
    ::close(client_fd);

    ctx.shutdown.store(true, std::memory_order_relaxed);
    // Wake the I/O thread via eventfd
    uint64_t val = 1;
    [[maybe_unused]] auto _ = ::write(ctx.eventfd_fd, &val, sizeof(val));
    ctx.server.request_shutdown();

    eng_thread.join();
    io_thread.join();

    std::printf("\nDone.\n");
    return 0;
}
