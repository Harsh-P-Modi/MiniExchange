#ifndef MINIEXCHANGE_ADAPTERS_TCP_TCP_SERVER_HPP
#define MINIEXCHANGE_ADAPTERS_TCP_TCP_SERVER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/Types.hpp"

namespace miniexchange::tcp {

// Maximum payload size per frame (design.md §5). A length prefix
// claiming more than this disconnects the client — prevents OOM from
// a malformed or malicious peer.
static constexpr uint32_t kMaxFrameSize = 4096;

// Default cap on a single connection's unsent outbound backlog
// (Phase 11 R4). A client that stops reading makes its write_buffer
// grow without bound as responses keep queuing; once the backlog would
// exceed this, that connection is dropped rather than allowed to
// consume memory without limit. 1 MiB is far above any legitimate
// single-burst response for this protocol, so a well-behaved client
// never approaches it. Pass 0 to the constructor to disable the bound
// (used by framing-layer unit tests that never exercise the write path).
static constexpr std::size_t kDefaultMaxWriteBufferBytes = 1u << 20;

// Callback invoked for each fully-framed message received from a
// client. Set via set_frame_handler(); if unset, frames are silently
// discarded. Task 5 wires this to the text protocol parser + queue push.
using FrameHandler = std::function<void(ClientId, std::string_view)>;

// Callback invoked when the eventfd fires (response data available in
// the outbound queue). The handler should drain the queue to exhaustion
// (try_pop in a loop), render + frame each response, and call
// send_to_client for each. Set via set_response_drain_handler().
using ResponseDrainHandler = std::function<void()>;

// Connection — per-client state owned exclusively by the I/O thread.
// No locking needed: the single epoll loop is the sole reader/writer
// of these fields.
struct Connection {
    int fd;                    // nonblocking, TCP_NODELAY
    ClientId id;               // ephemeral, assigned at accept()
    std::string read_buffer;   // partial frames accumulate here
    std::string write_buffer;  // unsent response bytes
};

// TcpServer — edge-triggered, nonblocking epoll-based TCP server.
//
// Manages listener socket creation, connection lifecycle (accept,
// read, write, close), and the epoll event loop. Queue integration
// (inbound/outbound SPSC queues, eventfd wakeup) is wired in
// subsequent tasks (4.2–6.x); this skeleton provides the structural
// foundation.
//
// Lifecycle:
//   1. Construct with a port (0 for OS-assigned ephemeral).
//   2. Call run() — blocks in the epoll loop until shutdown.
//   3. Signal shutdown via request_shutdown() (thread-safe).
//
// Non-copyable, non-movable: holds owned file descriptors (listener,
// epoll instance) whose lifetimes are tied to this object.
class TcpServer {
public:
    // port: 0 for an OS-assigned ephemeral port.
    // max_write_buffer_bytes: per-connection outbound backlog cap (R4).
    //   Defaults to kDefaultMaxWriteBufferBytes; 0 disables the bound.
    explicit TcpServer(uint16_t port,
                       std::size_t max_write_buffer_bytes =
                           kDefaultMaxWriteBufferBytes);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    // Start the epoll event loop (blocking — runs until shutdown).
    void run();

    // Signal shutdown (thread-safe: called from signal handler or
    // another thread). Causes run() to return on the next epoll
    // wakeup cycle.
    void request_shutdown();

    // Register a callback invoked for each complete frame received.
    // Must be called before run(). Not thread-safe — set once during
    // wiring, before the I/O thread starts.
    void set_frame_handler(FrameHandler handler);

    // Send data to a specific client (appends to write buffer, attempts
    // immediate flush). If the client has disconnected, silently drops
    // the data. Called from the I/O thread (typically after draining the
    // outbound response queue in later tasks).
    void send_to_client(ClientId client_id, std::string_view data);

    // Register an eventfd file descriptor for cross-thread wakeup.
    // The eventfd is registered with the epoll instance (EPOLLIN |
    // EPOLLET). When the engine thread writes to it, the I/O thread
    // wakes from epoll_wait and invokes the response drain handler.
    // Must be called before run(). Not thread-safe — set once during
    // wiring, before the I/O thread starts.
    void set_eventfd(int efd);

    // Register a callback invoked when the eventfd fires. The handler
    // should drain the outbound queue to exhaustion. Must be called
    // before run(). Not thread-safe — set once during wiring.
    void set_response_drain_handler(ResponseDrainHandler handler);

    // Get the actual listening port. Useful when port 0 was passed
    // (OS assigns an ephemeral port); valid after construction.
    uint16_t port() const;

private:
    uint16_t port_;
    std::size_t max_write_buffer_bytes_;  // R4: 0 = unbounded
    int listener_fd_ = -1;
    int epoll_fd_ = -1;
    std::atomic<bool> shutdown_requested_{false};
    uint64_t next_client_id_ = 1;  // monotonically increasing

    // fd → Connection (owned by the I/O thread, no concurrent access)
    std::unordered_map<int, Connection> connections_;
    // ClientId → fd (reverse lookup for response routing in later tasks)
    std::unordered_map<ClientId, int> client_to_fd_;

    // Frame handler callback — set once before run(), called per frame
    FrameHandler frame_handler_;

    // eventfd for cross-thread wakeup (engine → I/O thread). -1 if
    // not configured (e.g. unit tests that don't use queues).
    int eventfd_ = -1;

    // Response drain callback — invoked when eventfd fires
    ResponseDrainHandler response_drain_handler_;

    // Setup helpers
    void setup_listener();
    void setup_epoll();

    // Dispatch a complete frame to the registered handler (no-op if unset)
    void process_frame(ClientId client_id, std::string_view payload);

    // Write buffer management
    void flush_write_buffer(Connection& conn);
    void arm_epollout(int fd);
    void disarm_epollout(int fd);

    // Event handlers
    void handle_accept();
    void handle_read(int fd);
    void handle_write(int fd);
    void handle_eventfd();
    void close_connection(int fd);
};

}  // namespace miniexchange::tcp

#endif  // MINIEXCHANGE_ADAPTERS_TCP_TCP_SERVER_HPP
