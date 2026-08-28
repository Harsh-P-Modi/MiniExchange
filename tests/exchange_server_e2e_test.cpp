// tests/exchange_server_e2e_test.cpp — Full end-to-end integration test
// for the exchange server: real sockets, real engine, real queues, real
// eventfd wakeup. Validates the round-trip path:
//   client TCP → framing → parse → inbound queue → engine →
//   outbound queue → eventfd → drain → render → frame → client TCP
//
// Linux-only: requires epoll, eventfd, and real socket I/O.

#ifdef __linux__

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

#include "adapters/tcp/framing.hpp"
#include "adapters/tcp/tcp_server.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "adapters/text_protocol/text_protocol_renderer.hpp"
#include "core/EngineCommand.hpp"
#include "core/TaggedCommand.hpp"
#include "engine/matching_engine.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

namespace miniexchange {
namespace {

// --- Helpers ---

// Encode a payload into a 4-byte big-endian length-prefixed frame.
std::string make_frame(std::string_view payload) {
    auto len = static_cast<uint32_t>(payload.size());
    std::string frame(4 + len, '\0');
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >> 8) & 0xFF);
    frame[3] = static_cast<char>(len & 0xFF);
    std::memcpy(frame.data() + 4, payload.data(), len);
    return frame;
}

// Read exactly `n` bytes from fd with a timeout. Returns true on success.
bool read_exact(int fd, char* buf, std::size_t n,
                std::chrono::milliseconds timeout) {
    std::size_t total = 0;
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (total < n) {
        auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return false;

        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ret <= 0) return false;

        ssize_t r = ::recv(fd, buf + total, n - total, 0);
        if (r <= 0) return false;
        total += static_cast<std::size_t>(r);
    }
    return true;
}

// Receive a single length-prefixed frame from fd. Returns the payload
// (empty string on timeout/error).
std::string recv_frame(int fd,
                       std::chrono::milliseconds timeout =
                           std::chrono::milliseconds(3000)) {
    char header[4];
    if (!read_exact(fd, header, 4, timeout)) return {};

    uint32_t len = (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(header[3])));

    if (len == 0 || len > tcp::kMaxFrameSize) return {};

    std::string payload(len, '\0');
    if (!read_exact(fd, payload.data(), len, timeout)) return {};
    return payload;
}

// Send all bytes to a connected socket.
bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n =
            ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// --- Test Fixture ---

// ExchangeServerE2ETest — wires the complete exchange server stack in
// a test fixture: MatchingEngine + SPSC queues + TcpServer + eventfd.
// Spawns I/O and engine threads, tears down cleanly after each test.
class ExchangeServerE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create eventfd for cross-thread wakeup.
        eventfd_ = ::eventfd(0, EFD_NONBLOCK);
        ASSERT_GE(eventfd_, 0) << "eventfd() failed: " << strerror(errno);

        // Construct the server on an OS-assigned ephemeral port.
        server_ = std::make_unique<tcp::TcpServer>(0);

        // Wire frame handler: parse → push to inbound queue (or error
        // response directly).
        server_->set_frame_handler(
            [this](ClientId client_id, std::string_view payload) {
                auto result = text_protocol::parse(payload);
                std::visit(
                    [&](auto& val) {
                        using T = std::decay_t<decltype(val)>;
                        if constexpr (std::is_same_v<T,
                                                     text_protocol::ParseError>) {
                            auto err_msg =
                                text_protocol::render_error(val.message);
                            server_->send_to_client(
                                client_id, tcp::frame_message(err_msg));
                        } else {
                            TaggedCommand cmd{client_id, val};
                            inbound_.try_push(std::move(cmd));
                        }
                    },
                    result);
            });

        // Wire eventfd + drain handler.
        server_->set_eventfd(eventfd_);
        server_->set_response_drain_handler([this]() {
            TaggedResponse resp{};
            while (outbound_.try_pop(resp)) {
                auto rendered = text_protocol::render(resp.response);
                server_->send_to_client(resp.client,
                                        tcp::frame_message(rendered));
            }
        });

        // Start I/O thread.
        io_thread_ = std::thread([this] { server_->run(); });

        // Start engine thread.
        engine_thread_ = std::thread([this] {
            while (!shutdown_.load(std::memory_order_relaxed)) {
                TaggedCommand cmd{};
                if (!inbound_.try_pop(cmd)) {
                    std::this_thread::yield();
                    continue;
                }

                EngineResponse resp = std::visit(
                    [this](auto& command) -> EngineResponse {
                        using T = std::decay_t<decltype(command)>;
                        if constexpr (std::is_same_v<T, LimitOrder> ||
                                      std::is_same_v<T, MarketOrder>) {
                            return engine_.submit(command);
                        } else {
                            static_assert(std::is_same_v<T, CancelRequest>);
                            return engine_.cancel(command.id);
                        }
                    },
                    cmd.command);

                TaggedResponse tagged{cmd.client, std::move(resp)};
                while (outbound_.size() >= outbound_.capacity()) {
                    std::this_thread::yield();
                }
                outbound_.try_push(std::move(tagged));

                // Notify I/O thread.
                uint64_t val = 1;
                [[maybe_unused]] auto _ =
                    ::write(eventfd_, &val, sizeof(val));
            }
        });

        // Allow threads to settle.
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    void TearDown() override {
        // Signal shutdown.
        shutdown_.store(true, std::memory_order_relaxed);
        server_->request_shutdown();

        // Wake epoll_wait by connecting a dummy client.
        wake_server();

        if (engine_thread_.joinable()) engine_thread_.join();
        if (io_thread_.joinable()) io_thread_.join();

        if (eventfd_ >= 0) {
            ::close(eventfd_);
            eventfd_ = -1;
        }
    }

    // Connect a TCP client to the server, returns fd (-1 on failure).
    int connect_client() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(server_->port());
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr),
                      sizeof(addr)) < 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    // Send a framed command string to a connected client socket.
    bool send_command(int fd, std::string_view command) {
        return send_all(fd, make_frame(command));
    }

private:
    void wake_server() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(server_->port());
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::close(fd);
    }

    int eventfd_ = -1;
    std::unique_ptr<tcp::TcpServer> server_;
    MatchingEngine engine_;
    SpscRingBuffer<TaggedCommand, 4096> inbound_;
    SpscRingBuffer<TaggedResponse, 4096> outbound_;
    std::atomic<bool> shutdown_{false};
    std::thread io_thread_;
    std::thread engine_thread_;
};

// --- Test Cases ---

// A resting limit order gets accepted with no fills.
TEST_F(ExchangeServerE2ETest, LimitOrderRests) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    ASSERT_TRUE(send_command(fd, "ADD 1 BUY 100 10"));

    std::string response = recv_frame(fd);
    ASSERT_FALSE(response.empty()) << "Timed out waiting for response";

    // Expected: "ACCEPTED: no fills, remaining_qty=10\n"
    EXPECT_NE(response.find("ACCEPTED"), std::string::npos)
        << "Response: " << response;
    EXPECT_NE(response.find("remaining_qty=10"), std::string::npos)
        << "Response: " << response;
    EXPECT_NE(response.find("no fills"), std::string::npos)
        << "Response: " << response;

    ::close(fd);
}

// Two crossing limit orders produce a fill.
TEST_F(ExchangeServerE2ETest, LimitOrderCrosses) {
    int fd_a = connect_client();
    int fd_b = connect_client();
    ASSERT_GE(fd_a, 0);
    ASSERT_GE(fd_b, 0);

    // Client A: resting buy at 100 for 10 qty.
    ASSERT_TRUE(send_command(fd_a, "ADD 1 BUY 100 10"));
    std::string resp_a = recv_frame(fd_a);
    ASSERT_FALSE(resp_a.empty()) << "Timed out waiting for A's response";
    EXPECT_NE(resp_a.find("ACCEPTED"), std::string::npos)
        << "A's response: " << resp_a;

    // Client B: aggressive sell at 100 for 10 qty — crosses with A's buy.
    ASSERT_TRUE(send_command(fd_b, "ADD 2 SELL 100 10"));
    std::string resp_b = recv_frame(fd_b);
    ASSERT_FALSE(resp_b.empty()) << "Timed out waiting for B's response";

    // B should get a fill.
    EXPECT_NE(resp_b.find("ACCEPTED"), std::string::npos)
        << "B's response: " << resp_b;
    EXPECT_NE(resp_b.find("FILL"), std::string::npos)
        << "B's response: " << resp_b;
    EXPECT_NE(resp_b.find("10@100"), std::string::npos)
        << "B's response: " << resp_b;
    EXPECT_NE(resp_b.find("FULLY FILLED"), std::string::npos)
        << "B's response: " << resp_b;

    ::close(fd_a);
    ::close(fd_b);
}

// A market order sweeps a resting limit order.
TEST_F(ExchangeServerE2ETest, MarketOrder) {
    int fd_a = connect_client();
    int fd_b = connect_client();
    ASSERT_GE(fd_a, 0);
    ASSERT_GE(fd_b, 0);

    // Client A places a resting sell limit at 200 for 5 qty.
    ASSERT_TRUE(send_command(fd_a, "ADD 1 SELL 200 5"));
    std::string resp_a = recv_frame(fd_a);
    ASSERT_FALSE(resp_a.empty()) << "Timed out waiting for A's response";
    EXPECT_NE(resp_a.find("ACCEPTED"), std::string::npos)
        << "A's response: " << resp_a;

    // Client B sweeps with a market buy for 5 qty.
    ASSERT_TRUE(send_command(fd_b, "MARKET 2 BUY 5"));
    std::string resp_b = recv_frame(fd_b);
    ASSERT_FALSE(resp_b.empty()) << "Timed out waiting for B's response";

    // B should get a fill at price 200.
    EXPECT_NE(resp_b.find("ACCEPTED"), std::string::npos)
        << "B's response: " << resp_b;
    EXPECT_NE(resp_b.find("FILL"), std::string::npos)
        << "B's response: " << resp_b;
    EXPECT_NE(resp_b.find("5@200"), std::string::npos)
        << "B's response: " << resp_b;
    EXPECT_NE(resp_b.find("FULLY FILLED"), std::string::npos)
        << "B's response: " << resp_b;

    ::close(fd_a);
    ::close(fd_b);
}

// Cancelling a resting order.
TEST_F(ExchangeServerE2ETest, CancelOrder) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    // Place a resting limit order.
    ASSERT_TRUE(send_command(fd, "ADD 1 BUY 100 10"));
    std::string resp_add = recv_frame(fd);
    ASSERT_FALSE(resp_add.empty()) << "Timed out waiting for ADD response";
    EXPECT_NE(resp_add.find("ACCEPTED"), std::string::npos)
        << "ADD response: " << resp_add;

    // Cancel it.
    ASSERT_TRUE(send_command(fd, "CANCEL 1"));
    std::string resp_cancel = recv_frame(fd);
    ASSERT_FALSE(resp_cancel.empty())
        << "Timed out waiting for CANCEL response";

    // Cancel should be accepted.
    EXPECT_NE(resp_cancel.find("ACCEPTED"), std::string::npos)
        << "CANCEL response: " << resp_cancel;

    ::close(fd);
}

// A garbled command produces an ERROR response without engine round-trip.
TEST_F(ExchangeServerE2ETest, InvalidCommand) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    ASSERT_TRUE(send_command(fd, "GIBBERISH foo bar"));

    std::string response = recv_frame(fd);
    ASSERT_FALSE(response.empty()) << "Timed out waiting for error response";

    EXPECT_NE(response.find("ERROR:"), std::string::npos)
        << "Response: " << response;

    ::close(fd);
}

}  // namespace
}  // namespace miniexchange

#else
// Non-Linux: produce a single passing test so the binary is valid.
#include <gtest/gtest.h>
TEST(ExchangeServerE2ETest, SkippedOnNonLinux) {
    GTEST_SKIP() << "Exchange server E2E tests require Linux (epoll, eventfd)";
}
#endif  // __linux__
