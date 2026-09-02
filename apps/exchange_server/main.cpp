// apps/exchange_server/main.cpp — composition root for the TCP exchange server.
//
// Two threads, two SPSC queues:
//   - I/O thread: runs TcpServer's epoll loop (accept, read/parse, write)
//   - Engine thread (main thread): spins on inbound queue, processes commands,
//     pushes responses to outbound queue, notifies I/O thread via eventfd.
//
// Shutdown: SIGINT/SIGTERM → atomic flag + eventfd write → both threads exit.
//
// Protocol selection (Phase 7): --protocol=binary (default) or
// --protocol=plaintext (debug mode for manual netcat testing). Selected
// once at startup — every connection on this server instance speaks the
// same protocol. See specs/phase-07-binary-protocol/design.md §5.
//
// This is the multi-threaded, network-facing app in MiniExchange.
// It wires together:
//   - adapters/tcp/TcpServer (epoll loop, framing, connections)
//   - adapters/text_protocol OR adapters/binary_protocol (selected at startup)
//   - lockfree_queue/SpscRingBuffer (inbound + outbound queues)
//   - engine/MatchingEngine (single-threaded matching)
//   - core/TaggedCommand.hpp (queue payloads)
//   - eventfd (cross-thread wakeup notification)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "adapters/tcp/framing.hpp"
#include "adapters/tcp/tcp_server.hpp"
#include "apps/exchange_server/engine_loop.hpp"
#include "adapters/binary_protocol/GatewayCodec.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "adapters/text_protocol/text_protocol_renderer.hpp"
#include "adapters/udp/queued_event_sink.hpp"
#include "adapters/udp/udp_feed_publisher.hpp"
#include "core/EngineCommand.hpp"
#include "core/TaggedCommand.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/engine_api.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_engine.hpp"

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
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg.starts_with("--protocol=")) continue;  // skip protocol flag
        int port = std::atoi(argv[i]);
        if (port > 0 && port <= 65535) {
            return static_cast<uint16_t>(port);
        }
        std::fprintf(stderr, "Invalid port: %s (using default 9000)\n",
                     argv[i]);
    }
    return 9000;
}

// Parse --protocol=binary|plaintext from argv. Default: binary.
enum class ProtocolMode { Binary, Plaintext };

static ProtocolMode parse_protocol(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--protocol=plaintext") return ProtocolMode::Plaintext;
        if (arg == "--protocol=binary") return ProtocolMode::Binary;
    }
    return ProtocolMode::Binary;  // default
}

int main(int argc, char* argv[]) {
    using namespace miniexchange;
    using namespace miniexchange::tcp;
    using namespace miniexchange::text_protocol;

    const uint16_t port = parse_port(argc, argv);
    const ProtocolMode protocol = parse_protocol(argc, argv);

    // --- Protocol function slots (bound once at startup) ---
    // These have the same signatures as text_protocol::parse/render/render_error
    // so the frame handler and drain handler lambdas work identically
    // regardless of which protocol was selected.
    using ParseFn = std::function<text_protocol::ParseResult(std::string_view)>;
    using RenderFn = std::function<std::string(const EngineResponse&)>;
    using RenderErrorFn = std::function<std::string(const std::string&)>;

    ParseFn parse_fn;
    RenderFn render_fn;
    RenderErrorFn render_error_fn;

    if (protocol == ProtocolMode::Binary) {
        parse_fn = binary_protocol::parse_binary;
        render_fn = binary_protocol::render_binary;
        render_error_fn = binary_protocol::render_binary_error;
    } else {
        parse_fn = text_protocol::parse;
        render_fn = text_protocol::render;
        render_error_fn = text_protocol::render_error;
    }

    // --- Create eventfd (nonblocking for edge-triggered epoll) ---
    g_eventfd = ::eventfd(0, EFD_NONBLOCK);
    if (g_eventfd < 0) {
        std::fprintf(stderr, "eventfd() failed: %s\n", std::strerror(errno));
        return 1;
    }

    // --- Create UDP feed publisher (Phase 6) ---
    // Default: publish to localhost:9001. Override with env var
    // MINIEXCHANGE_UDP_FEED_PORT if needed.
    const char* feed_port_env = std::getenv("MINIEXCHANGE_UDP_FEED_PORT");
    uint16_t feed_port = feed_port_env ? static_cast<uint16_t>(std::atoi(feed_port_env)) : 9001;

    int udp_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udp_fd < 0) {
        std::fprintf(stderr, "UDP socket() failed: %s\n", std::strerror(errno));
        return 1;
    }

    // Single subscriber: localhost at feed_port (simulated unicast fan-out)
    miniexchange::udp::Subscriber sub{};
    sub.addr.sin_family = AF_INET;
    sub.addr.sin_port = htons(feed_port);
    sub.addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    constexpr miniexchange::SymbolId kSymbol{1};
    miniexchange::udp::UdpFeedPublisher feed_publisher(
        kSymbol, {sub}, udp_fd, 500 /* snapshot every 500 messages */);

    // --- Phase 11 R5: decouple feed publication from the match call stack ---
    // The engine is wired to QueuedEventSink, NOT to feed_publisher
    // directly. QueuedEventSink::on_trade/on_order_* just copy a POD into
    // an SPSC ring and return — no sendto() on the engine thread's stack.
    // A dedicated thread owns feed_publisher and drains that ring,
    // performing the actual UDP I/O from its own stack. If the ring
    // fills, events are dropped and counted (queued_sink.dropped()) — a
    // missed market-data tick is the right failure mode for a feed; the
    // engine thread is never blocked on feed-queue space.
    miniexchange::udp::QueuedEventSink<> queued_sink;
    std::atomic<bool> feed_stop{false};
    std::thread feed_thread([&queued_sink, &feed_publisher, &feed_stop]() {
        miniexchange::udp::run_feed_publisher(queued_sink, feed_publisher,
                                              feed_stop);
    });

    // --- Construct engine + risk layer (Phase 8) ---
    // RiskConfig is the single composition-root risk configuration. Its
    // STP slice is passed down into the engine (STP executes inside the
    // match loop — see specs/phase-08-risk-engine/design.md §5); the
    // three pre-trade checks (fat-finger, tick-size, price-band) run in
    // the RiskEngine decorator that wraps the engine.
    RiskConfig risk_config{};
    risk_config.price_band_pct = 0.10;                  // +/-10% band
    risk_config.initial_reference_price = Price{10000};  // Q4 cold-start seed
    risk_config.max_order_qty = Quantity{1'000'000};     // fat-finger ceiling
    risk_config.tick_size = Price{1};
    risk_config.stp_enabled = true;
    risk_config.stp_policy = StpPolicy::RejectIncoming;

    MatchingEngine matching_engine(&queued_sink, 1'000'000,
                                   risk_config.stp());

    // The decorator is the ONLY entry point the rest of the app uses.
    // Everything downstream is typed as EngineAPI&, so nothing can
    // accidentally bypass the risk checks by calling the concrete engine
    // directly — the single-entry-point constraint design.md §7 relies on
    // for NFR2.
    RiskEngine risk_engine(&matching_engine, risk_config);
    EngineAPI& engine = risk_engine;

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
        [&inbound, &server, &parse_fn, &render_error_fn](
            ClientId client_id, std::string_view payload) {
            auto result = parse_fn(payload);
            std::visit(
                [&](auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, ParseError>) {
                        // Protocol-level error: respond directly without
                        // engine round-trip.
                        auto err_msg = render_error_fn(val.message);
                        server.send_to_client(client_id,
                                              frame_message(err_msg));
                    } else {
                        // Valid command: push to inbound queue for engine.
                        // Phase 11 R1: the inbound queue is bounded (no
                        // blocking — Phase 5 R4), but a full queue must not
                        // swallow the order silently. try_push returns false
                        // when full; on that, respond directly from the I/O
                        // thread using the same render/frame/send path the
                        // ParseError branch above already uses. Routing this
                        // rejection *through* the engine is impossible — the
                        // queue it would travel on is the one that is full —
                        // so the direct I/O-thread response is the only
                        // option that does not depend on the exhausted
                        // resource. See specs/phase-11-iteration/design.md §1.
                        TaggedCommand cmd{client_id, val};
                        if (!inbound.try_push(std::move(cmd))) {
                            auto err_msg = render_error_fn(
                                "system busy — order not accepted");
                            server.send_to_client(client_id,
                                                  frame_message(err_msg));
                        }
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
        [&outbound, &server, &render_fn]() {
            TaggedResponse resp{};
            while (outbound.try_pop(resp)) {
                auto rendered = render_fn(resp.response);
                server.send_to_client(resp.client, frame_message(rendered));
            }
        });

    // --- Install signal handlers (after eventfd is created) ---
    install_signal_handlers();

    // --- Spawn I/O thread ---
    std::thread io_thread([&server]() { server.run(); });

    std::fprintf(stderr, "exchange_server listening on port %u (%s protocol)\n",
                port, protocol == ProtocolMode::Binary ? "binary" : "plaintext");

    // --- Engine thread (main thread) ---
    // Spins on inbound queue, processes commands through the engine,
    // pushes responses to outbound queue, and notifies I/O thread.
    //
    // Phase 11 R6: the eventfd wakeup is coalesced. Instead of one
    // ::write(g_eventfd) per response, WakeupCoalescer defers the write
    // to the moment the inbound queue reads empty — one syscall per
    // "drain until idle" cycle. The I/O thread's drain handler already
    // empties the outbound queue in one call, so fewer wakeups flush the
    // same responses; under light load the queue reads empty right after
    // the single response, so the wakeup still fires with no added
    // latency. See apps/exchange_server/engine_loop.hpp.
    exchange_server::WakeupCoalescer wakeup;
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        TaggedCommand cmd{};
        if (!inbound.try_pop(cmd)) {
            // Inbound queue drained — issue the coalesced wakeup now if
            // any response was pushed since the last one.
            if (wakeup.should_notify_on_idle()) {
                uint64_t val = 1;
                [[maybe_unused]] auto _ =
                    ::write(g_eventfd, &val, sizeof(val));
            }
            // No work — yield to avoid pure busy-spin burning CPU.
            // A more aggressive approach (e.g. _mm_pause or spinning N
            // times before yielding) is a Phase 7+ optimization.
            std::this_thread::yield();
            continue;
        }

        // Dispatch to engine (Phase 8: stamps cmd.client as the order's
        // owner for STP; Phase 11 R3: catches any throw out of engine
        // dispatch at this boundary and degrades it to an InternalError
        // response instead of terminating the process). See
        // apps/exchange_server/engine_loop.hpp.
        EngineResponse resp = exchange_server::dispatch_command(engine, cmd);

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

        // Phase 11 R6: record that a response is pending, but DON'T write
        // the eventfd here. The write is issued once, above, when the
        // inbound queue next reads empty — coalescing a burst of
        // responses into a single I/O-thread wakeup.
        wakeup.note_response();
    }

    // A response may have been pushed on the final iteration before the
    // shutdown flag was observed; make sure the I/O thread is woken to
    // flush it (it also needs waking to see request_shutdown()).
    if (wakeup.should_notify_on_idle()) {
        uint64_t val = 1;
        [[maybe_unused]] auto _ = ::write(g_eventfd, &val, sizeof(val));
    }

    // --- Shutdown ---
    // Signal the I/O thread to exit (in case it hasn't received the
    // eventfd write from the signal handler yet).
    server.request_shutdown();
    io_thread.join();

    // Stop the feed-publisher thread. run_feed_publisher does one final
    // drain after observing the stop flag, so any events queued between
    // the last engine command and here are still published before the
    // UDP socket is closed.
    feed_stop.store(true, std::memory_order_relaxed);
    feed_thread.join();

    ::close(g_eventfd);
    g_eventfd = -1;

    ::close(udp_fd);

    std::fprintf(stderr, "exchange_server shutdown complete\n");
    return 0;
}
