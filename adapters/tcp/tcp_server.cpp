#include "adapters/tcp/tcp_server.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace miniexchange::tcp {

// Maximum events returned per epoll_wait call. 64 is generous for
// the expected client count (tens, not thousands); keeps the stack
// allocation small while avoiding multiple epoll_wait calls per
// iteration under normal load.
static constexpr int kMaxEvents = 64;

TcpServer::TcpServer(uint16_t port) : port_(port) {
    setup_listener();
    setup_epoll();
}

TcpServer::~TcpServer() {
    // Close all client connections
    for (auto& [fd, conn] : connections_) {
        ::close(fd);
    }
    connections_.clear();
    client_to_fd_.clear();

    // Close listener and epoll fds
    if (listener_fd_ >= 0) {
        ::close(listener_fd_);
        listener_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

void TcpServer::setup_listener() {
    // Create TCP socket (nonblocking from the start via SOCK_NONBLOCK)
    listener_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listener_fd_ < 0) {
        throw std::runtime_error(
            std::string("socket() failed: ") + std::strerror(errno));
    }

    // SO_REUSEADDR — allows quick restarts during testing without
    // waiting for TIME_WAIT to expire on the port.
    int optval = 1;
    if (::setsockopt(listener_fd_, SOL_SOCKET, SO_REUSEADDR,
                     &optval, sizeof(optval)) < 0) {
        ::close(listener_fd_);
        listener_fd_ = -1;
        throw std::runtime_error(
            std::string("setsockopt(SO_REUSEADDR) failed: ") +
            std::strerror(errno));
    }

    // Bind to 0.0.0.0:<port>. Port 0 means the OS picks an
    // ephemeral port (useful for tests that run in parallel).
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listener_fd_, reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        ::close(listener_fd_);
        listener_fd_ = -1;
        throw std::runtime_error(
            std::string("bind() failed: ") + std::strerror(errno));
    }

    // Retrieve actual port (needed when port_ was 0)
    socklen_t addr_len = sizeof(addr);
    if (::getsockname(listener_fd_, reinterpret_cast<sockaddr*>(&addr),
                      &addr_len) < 0) {
        ::close(listener_fd_);
        listener_fd_ = -1;
        throw std::runtime_error(
            std::string("getsockname() failed: ") + std::strerror(errno));
    }
    port_ = ntohs(addr.sin_port);

    // Listen with a reasonable backlog. 128 is the common default
    // (SOMAXCONN on most Linux systems); fine for this project's
    // expected concurrency.
    if (::listen(listener_fd_, 128) < 0) {
        ::close(listener_fd_);
        listener_fd_ = -1;
        throw std::runtime_error(
            std::string("listen() failed: ") + std::strerror(errno));
    }
}

void TcpServer::setup_epoll() {
    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error(
            std::string("epoll_create1() failed: ") + std::strerror(errno));
    }

    // Register the listener fd with epoll (edge-triggered, readable).
    // Edge-triggered on the listener means we must drain accept() in
    // a loop until EAGAIN on each notification (handled in
    // handle_accept).
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listener_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener_fd_, &ev) < 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
        throw std::runtime_error(
            std::string("epoll_ctl(listener) failed: ") +
            std::strerror(errno));
    }
}

void TcpServer::run() {
    epoll_event events[kMaxEvents];

    while (!shutdown_requested_.load(std::memory_order_relaxed)) {
        // Block indefinitely — woken by client events, eventfd
        // (engine thread notifications), or signal interrupts.
        int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, -1);

        if (n < 0) {
            if (errno == EINTR) {
                // Interrupted by signal — check shutdown flag and retry.
                continue;
            }
            throw std::runtime_error(
                std::string("epoll_wait() failed: ") + std::strerror(errno));
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listener_fd_) {
                handle_accept();
            } else if (fd == eventfd_) {
                handle_eventfd();
                // Check shutdown after eventfd wakeup — shutdown writes
                // to eventfd to unblock epoll_wait(-1).
                if (shutdown_requested_.load(std::memory_order_relaxed)) {
                    goto shutdown;
                }
            } else if (ev & (EPOLLHUP | EPOLLERR)) {
                close_connection(fd);
            } else {
                if (ev & EPOLLIN) {
                    handle_read(fd);
                }
                if (ev & EPOLLOUT) {
                    handle_write(fd);
                }
            }
        }
    }

shutdown:
    // Shutdown: close all remaining connections gracefully.
    for (auto& [fd, conn] : connections_) {
        ::close(fd);
    }
    connections_.clear();
    client_to_fd_.clear();
}

void TcpServer::request_shutdown() {
    shutdown_requested_.store(true, std::memory_order_relaxed);
    // Write to eventfd to unblock epoll_wait(-1). The I/O thread checks
    // the shutdown flag after handling the eventfd wakeup.
    if (eventfd_ >= 0) {
        uint64_t val = 1;
        [[maybe_unused]] auto _ = ::write(eventfd_, &val, sizeof(val));
    }
}

void TcpServer::set_frame_handler(FrameHandler handler) {
    frame_handler_ = std::move(handler);
}

void TcpServer::set_eventfd(int efd) {
    eventfd_ = efd;

    // Register the eventfd with the existing epoll instance
    // (edge-triggered, read-only). The engine thread writes to it to
    // wake the I/O thread when outbound responses are available.
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = efd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, efd, &ev) < 0) {
        throw std::runtime_error(
            std::string("epoll_ctl(eventfd) failed: ") +
            std::strerror(errno));
    }
}

void TcpServer::set_response_drain_handler(ResponseDrainHandler handler) {
    response_drain_handler_ = std::move(handler);
}

uint16_t TcpServer::port() const {
    return port_;
}

// --- Event handlers ---

void TcpServer::handle_eventfd() {
    // Read the 8-byte counter to clear the eventfd (edge-triggered:
    // must read to re-arm). The counter value is irrelevant — we
    // always drain the outbound queue to exhaustion regardless of
    // how many writes coalesced.
    uint64_t val = 0;
    [[maybe_unused]] auto _ = ::read(eventfd_, &val, sizeof(val));

    // Invoke the drain handler if registered. The handler calls
    // try_pop in a loop, renders + frames each response, and calls
    // send_to_client for each.
    if (response_drain_handler_) {
        response_drain_handler_();
    }
}

void TcpServer::handle_accept() {
    // Edge-triggered: must drain all pending connections in a loop.
    // accept4() returns EAGAIN/EWOULDBLOCK when no more are queued.
    while (true) {
        int client_fd = ::accept4(listener_fd_, nullptr, nullptr, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No more pending connections — drain complete
            }
            // Transient errors (EMFILE, ENFILE, ECONNABORTED) — skip
            // this iteration but keep draining; EINTR is retryable.
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // Set TCP_NODELAY — disable Nagle's algorithm (R2: latency
        // over throughput). If this fails, close the socket rather
        // than silently operate with Nagle enabled.
        int optval = 1;
        if (::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                         &optval, sizeof(optval)) < 0) {
            ::close(client_fd);
            continue;
        }

        // Assign a monotonically increasing ephemeral ClientId.
        // No atomics needed — only the I/O thread ever calls accept.
        ClientId client_id{next_client_id_++};

        // Create per-connection state and store in both lookup maps.
        Connection conn{client_fd, client_id, {}, {}};
        connections_.emplace(client_fd, std::move(conn));
        client_to_fd_.emplace(client_id, client_fd);

        // Register with epoll (edge-triggered, initially read-only).
        // EPOLLOUT is armed later only when data is pending in the
        // write buffer (Task 4.5).
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            // Registration failed — clean up the connection we just added
            client_to_fd_.erase(client_id);
            connections_.erase(client_fd);
            ::close(client_fd);
        }
    }
}

void TcpServer::handle_read(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    auto& conn = it->second;

    // Edge-triggered: must drain all available data in a loop.
    // 4096 matches kMaxFrameSize — no single read can exceed one
    // max-frame payload, keeping stack usage bounded.
    char buf[4096];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            conn.read_buffer.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
            // EOF — peer closed the connection gracefully.
            close_connection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // Drain complete — no more data available
            }
            if (errno == EINTR) {
                continue;  // Interrupted by signal — retry immediately
            }
            // Genuine read error — drop the connection.
            close_connection(fd);
            return;
        }
    }

    // Process complete frames from read_buffer.
    // Frame format: [4 bytes big-endian length][payload of that length]
    while (conn.read_buffer.size() >= 4) {
        // Decode length prefix (network byte order = big-endian).
        const auto* p = reinterpret_cast<const uint8_t*>(conn.read_buffer.data());
        uint32_t payload_len = (static_cast<uint32_t>(p[0]) << 24)
                             | (static_cast<uint32_t>(p[1]) << 16)
                             | (static_cast<uint32_t>(p[2]) << 8)
                             | (static_cast<uint32_t>(p[3]));

        // Reject oversized frames — protocol violation (design.md §5).
        if (payload_len > kMaxFrameSize) {
            close_connection(fd);
            return;
        }

        // Check if the full frame has arrived yet.
        if (conn.read_buffer.size() < 4 + payload_len) {
            break;  // Partial frame — wait for more data
        }

        // Extract payload and dispatch.
        std::string_view payload(conn.read_buffer.data() + 4, payload_len);
        process_frame(conn.id, payload);

        // Erase the consumed frame from the buffer.
        conn.read_buffer.erase(0, 4 + payload_len);
    }
}

void TcpServer::process_frame(ClientId client_id, std::string_view payload) {
    if (frame_handler_) {
        frame_handler_(client_id, payload);
    }
    // If no handler is registered, silently discard — this is the
    // expected state during unit testing of the framing layer itself,
    // before Task 5 wires in the text protocol parser.
}

void TcpServer::handle_write(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    auto& conn = it->second;

    flush_write_buffer(conn);
}

void TcpServer::flush_write_buffer(Connection& conn) {
    while (!conn.write_buffer.empty()) {
        ssize_t n = ::send(conn.fd, conn.write_buffer.data(),
                           conn.write_buffer.size(), MSG_NOSIGNAL);
        if (n > 0) {
            conn.write_buffer.erase(0, static_cast<std::size_t>(n));
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel send buffer full — arm EPOLLOUT so epoll
                // wakes us when the socket becomes writable again.
                arm_epollout(conn.fd);
                return;
            }
            if (errno == EINTR) {
                continue;  // Interrupted by signal — retry immediately
            }
            // Genuine write error (EPIPE, ECONNRESET, etc.) — drop
            // the connection.
            close_connection(conn.fd);
            return;
        }
    }
    // Buffer fully drained — disarm EPOLLOUT to avoid busy-looping
    // on an always-writable socket.
    disarm_epollout(conn.fd);
}

void TcpServer::arm_epollout(int fd) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void TcpServer::disarm_epollout(int fd) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void TcpServer::send_to_client(ClientId client_id, std::string_view data) {
    auto it = client_to_fd_.find(client_id);
    if (it == client_to_fd_.end()) return;  // client disconnected
    int fd = it->second;

    auto conn_it = connections_.find(fd);
    if (conn_it == connections_.end()) return;
    auto& conn = conn_it->second;

    conn.write_buffer.append(data);
    flush_write_buffer(conn);
}

void TcpServer::close_connection(int fd) {
    // Remove from epoll before closing to prevent stale event delivery
    // if the fd number is reused by a subsequent accept().
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        client_to_fd_.erase(it->second.id);
        connections_.erase(it);
    }
    ::close(fd);
}

}  // namespace miniexchange::tcp
