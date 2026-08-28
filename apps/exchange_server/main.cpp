// apps/exchange_server/main.cpp — composition root for the TCP exchange server.
//
// Two threads, two SPSC queues:
//   - I/O thread: runs TcpServer's epoll loop (accept, read/parse, write)
//   - Engine thread (main thread): spins on inbound queue, processes commands,
//     pushes responses to outbound queue, notifies I/O thread via eventfd.
//
// Shutdown: SIGINT/SIGTERM → atomic flag + eventfd write → both threads exit.
//
// This is the first multi-threaded, network-facing app in MiniExchange.
// It wires together all Phase 5 components:
//   - adapters/tcp/TcpServer (epoll loop, framing, connections)
//   - adapters/text_protocol (parse commands, render responses)
//   - lockfree_queue/SpscRingBuffer (inbound + outbound queues)
//   - engine/MatchingEngine (single-threaded matching)
//   - core/TaggedCommand.hpp (queue payloads)
//   - eventfd (cross-thread wakeup notification)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <variant>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "adapters/tcp/framing.hpp"
#include "adapters/tcp/tcp_server.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "adapters/text_protocol/text_protocol_renderer.hpp"
#include "core/EngineCommand.hpp"
#include "core/TaggedCommand.hpp"
#include "engine/matching_engine.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

// --- Global shutdown state ---
// Accessed from signal handler + engine thread + (indirectly) I/O thread.
// Relaxed ordering suffices: the eventfd write provides the cross-thread
// visibility guarantee that actually unblocks epoll_wait.
static std::atomic<bool> g_shutdown{false};
static int g_eventfd = -1;

static void signal_handler(int /*signo*/) {
    g_shutdown.store(true, std::memory_order_relaxed);
    if (g_eventfd >= 0) {
        uint64_t val = 1;
        // write() is async-signal-safe on Linux.
        [[maybe_unused]] auto _ = ::write(g_eventfd, &val, sizeof(val));
    }
}

static void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;  // No SA_RESTART — we want epoll_wait to be interrupted
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

static uint16_t parse_port(int argc, char* argv[]) {
    if (argc >= 2) {
        int port = std::atoi(argv[1]);
        if (port > 0 && port <= 65535) {
            return static_cast<uint16_t>(port);
        }
        std::fprintf(stderr, "Invalid port: %s (using default 9000)\n",
                     argv[1]);
    }
    return 9000;
}

int main(int argc, char* argv[]) {
    using namespace miniexchange;
    using namespace miniexchange::tcp;
    using namespace miniexchange::text_protocol;

    const uint16_t port = parse_port(argc, argv);

    // --- Create eventfd (nonblocking for edge-triggered epoll) ---
    g_eventfd = ::eventfd(0, EFD_NONBLOCK);
    if (g_eventfd < 0) {
        std::fprintf(stderr, "eventfd() failed: %s\n", std::strerror(errno));
        return 1;
    }

    // --- Construct engine (NullEventSink — Phase 6 adds a real sink) ---
    MatchingEngine engine;

    // --- Construct SPSC queues ---
    // Inbound: I/O thread (producer) → engine thread (consumer)
    SpscRingBuffer<TaggedCommand, 4096> inbound;
    // Outbound: engine thread (producer) → I/O thread (consumer)
    SpscRingBuffer<TaggedResponse, 65536> outbound;

    // --- Construct TcpServer ---
    TcpServer server(port);

    // Wire the frame handler: parse incoming messages, push valid
    // commands to inbound queue, send parse errors directly back.
    server.set_frame_handler(
        [&inbound, &server](ClientId client_id, std::string_view payload) {
            auto result = parse(payload);
            std::visit(
                [&](auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, ParseError>) {
                        // Protocol-level error: respond directly without
                        // engine round-trip (design.md §4, step 3).
                        auto err_msg = render_error(val.message);
                        server.send_to_client(client_id,
                                              frame_message(err_msg));
                    } else {
                        // Valid command: push to inbound queue for engine.
                        // Drop on full (R4 — bounded queue, no blocking).
                        TaggedCommand cmd{client_id, val};
                        inbound.try_push(std::move(cmd));
                    }
                },
                result);
        });

    // Wire the eventfd: registered with epoll so the I/O thread wakes
    // when outbound responses are available.
    server.set_eventfd(g_eventfd);

    // Wire the response drain handler: called when eventfd fires.
    // Drains the outbound queue to exhaustion, renders + frames each
    // response, and sends to the correct client.
    server.set_response_drain_handler(
        [&outbound, &server]() {
            TaggedResponse resp{};
            while (outbound.try_pop(resp)) {
                auto rendered = render(resp.response);
                server.send_to_client(resp.client, frame_message(rendered));
            }
        });

    // --- Install signal handlers (after eventfd is created) ---
    install_signal_handlers();

    // --- Spawn I/O thread ---
    std::thread io_thread([&server]() { server.run(); });

    std::fprintf(stderr, "exchange_server listening on port %u\n", port);

    // --- Engine thread (main thread) ---
    // Spins on inbound queue, processes commands through the engine,
    // pushes responses to outbound queue, and notifies I/O thread.
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        TaggedCommand cmd{};
        if (!inbound.try_pop(cmd)) {
            // No work — yield to avoid pure busy-spin burning CPU.
            // A more aggressive approach (e.g. _mm_pause or spinning N
            // times before yielding) is a Phase 7+ optimization.
            std::this_thread::yield();
            continue;
        }

        // Dispatch to engine via std::visit on the EngineCommand variant.
        EngineResponse resp = std::visit(
            [&engine](auto& command) -> EngineResponse {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, LimitOrder> ||
                              std::is_same_v<T, MarketOrder>) {
                    return engine.submit(command);
                } else {
                    static_assert(std::is_same_v<T, CancelRequest>);
                    return engine.cancel(command.id);
                }
            },
            cmd.command);

        // Push response to outbound queue. R8: spin-retry on full —
        // the engine must never discard a response (unlike inbound,
        // where dropping is acceptable under extreme load).
        //
        // Spin-yield until space is available, then move-push once.
        // We check size() first (safe: we're the sole producer, so
        // the queue can only shrink between our check and the push).
        // With a 65536-slot queue and one producer, this spin is
        // effectively unreachable under normal load.
        TaggedResponse tagged{cmd.client, std::move(resp)};
        while (outbound.size() >= outbound.capacity()) {
            std::this_thread::yield();
        }
        outbound.try_push(std::move(tagged));  // guaranteed to succeed

        // Notify I/O thread that a response is available.
        uint64_t val = 1;
        [[maybe_unused]] auto _ = ::write(g_eventfd, &val, sizeof(val));
    }

    // --- Shutdown ---
    // Signal the I/O thread to exit (in case it hasn't received the
    // eventfd write from the signal handler yet).
    server.request_shutdown();
    io_thread.join();

    ::close(g_eventfd);
    g_eventfd = -1;

    std::fprintf(stderr, "exchange_server shutdown complete\n");
    return 0;
}
