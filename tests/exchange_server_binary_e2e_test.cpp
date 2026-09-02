// tests/exchange_server_binary_e2e_test.cpp — Task 12: Full end-to-end
// integration test for the exchange server in binary protocol mode.
// Same architecture as exchange_server_e2e_test.cpp but wires
// binary_protocol::parse_binary / render_binary / render_binary_error.
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
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "adapters/binary_protocol/BinaryCodec.hpp"
#include "adapters/binary_protocol/GatewayCodec.hpp"
#include "adapters/binary_protocol/Message.hpp"
#include "adapters/tcp/framing.hpp"
#include "adapters/tcp/tcp_server.hpp"
#include "adapters/text_protocol/text_protocol_parser.hpp"
#include "core/EngineCommand.hpp"
#include "core/TaggedCommand.hpp"
#include "engine/matching_engine.hpp"
#include "lockfree_queue/spsc_ring_buffer.hpp"

namespace miniexchange {
namespace {

// --- Helpers (same framing helpers as the plaintext E2E test) ---

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

// Receive raw bytes of a single framed message (strips 4-byte length prefix).
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

// --- Binary-specific helpers ---

// Encode a binary message struct, frame it, and send.
template <typename Msg>
bool send_binary_msg(int fd, const Msg& msg) {
    std::array<std::byte, binary_protocol::kMaxMessageWireSize> buf{};
    std::size_t written = binary_protocol::encode(msg, buf);
    std::string_view payload(reinterpret_cast<const char*>(buf.data()), written);
    return send_all(fd, make_frame(payload));
}

// Receive a frame and decode as a binary message variant.
std::optional<binary_protocol::AnyMessage> recv_binary_msg(
    int fd, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    std::string payload = recv_frame(fd, timeout);
    if (payload.empty()) return std::nullopt;
    return binary_protocol::decode(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
}

// Wire size of one server->client message by its leading type byte.
std::size_t binary_msg_wire_size(std::uint8_t type_byte) {
    switch (static_cast<binary_protocol::MessageType>(type_byte)) {
        case binary_protocol::MessageType::Ack:
            return binary_protocol::kAckWireSize;
        case binary_protocol::MessageType::Reject:
            return binary_protocol::kRejectWireSize;
        case binary_protocol::MessageType::TradeNotification:
            return binary_protocol::kTradeNotificationWireSize;
        default:
            return 0;
    }
}

// Receive ONE frame and decode EVERY message packed into it. render_binary
// emits an AckMsg immediately followed by zero or more TradeNotificationMsgs
// as a single concatenated payload, framed once — so a crossing order's
// ack + fills arrive together, not as separate frames.
std::vector<binary_protocol::AnyMessage> recv_binary_msgs(
    int fd, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    std::vector<binary_protocol::AnyMessage> out;
    std::string payload = recv_frame(fd, timeout);
    std::size_t off = 0;
    while (off < payload.size()) {
        std::size_t sz =
            binary_msg_wire_size(static_cast<std::uint8_t>(payload[off]));
        if (sz == 0 || off + sz > payload.size()) break;
        auto m = binary_protocol::decode(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(payload.data() + off), sz));
        if (!m.has_value()) break;
        out.push_back(*m);
        off += sz;
    }
    return out;
}

// --- Test Fixture ---

class BinaryExchangeServerE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        eventfd_ = ::eventfd(0, EFD_NONBLOCK);
        ASSERT_GE(eventfd_, 0) << "eventfd() failed: " << strerror(errno);

        server_ = std::make_unique<tcp::TcpServer>(0);

        // Wire frame handler with BINARY protocol functions.
        server_->set_frame_handler(
            [this](ClientId client_id, std::string_view payload) {
                auto result = binary_protocol::parse_binary(payload);
                std::visit(
                    [&](auto& val) {
                        using T = std::decay_t<decltype(val)>;
                        if constexpr (std::is_same_v<T,
                                                     text_protocol::ParseError>) {
                            auto err_msg =
                                binary_protocol::render_binary_error(val.message);
                            server_->send_to_client(
                                client_id, tcp::frame_message(err_msg));
                        } else {
                            TaggedCommand cmd{client_id, val};
                            inbound_.try_push(std::move(cmd));
                        }
                    },
                    result);
            });

        server_->set_eventfd(eventfd_);
        server_->set_response_drain_handler([this]() {
            TaggedResponse resp{};
            while (outbound_.try_pop(resp)) {
                auto rendered = binary_protocol::render_binary(resp.response);
                server_->send_to_client(resp.client,
                                        tcp::frame_message(rendered));
            }
        });

        io_thread_ = std::thread([this] { server_->run(); });

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

                uint64_t val = 1;
                [[maybe_unused]] auto _ =
                    ::write(eventfd_, &val, sizeof(val));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    void TearDown() override {
        shutdown_.store(true, std::memory_order_relaxed);
        server_->request_shutdown();
        wake_server();

        if (engine_thread_.joinable()) engine_thread_.join();
        if (io_thread_.joinable()) io_thread_.join();

        if (eventfd_ >= 0) {
            ::close(eventfd_);
            eventfd_ = -1;
        }
    }

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

// A resting limit order gets accepted with no fills → AckMsg.
TEST_F(BinaryExchangeServerE2ETest, LimitOrderRests) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    binary_protocol::LimitOrderAddMsg msg;
    msg.type = binary_protocol::MessageType::LimitOrderAdd;
    msg.side = 0;  // Buy
    msg.client_id = ClientId{1};
    msg.order_id = OrderId{1};
    msg.price = Price{100};
    msg.quantity = Quantity{10};
    ASSERT_TRUE(send_binary_msg(fd, msg));

    auto response = recv_binary_msg(fd);
    ASSERT_TRUE(response.has_value()) << "Timed out waiting for response";
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(*response));

    auto& ack = std::get<binary_protocol::AckMsg>(*response);
    EXPECT_EQ(ack.remaining_qty, Quantity{10});

    ::close(fd);
}

// Two crossing limit orders produce a fill → AckMsg + TradeNotificationMsg.
TEST_F(BinaryExchangeServerE2ETest, LimitOrderCrosses) {
    int fd_a = connect_client();
    int fd_b = connect_client();
    ASSERT_GE(fd_a, 0);
    ASSERT_GE(fd_b, 0);

    // Client A: resting buy at 100 for 10 qty.
    binary_protocol::LimitOrderAddMsg buy_msg;
    buy_msg.type = binary_protocol::MessageType::LimitOrderAdd;
    buy_msg.side = 0;  // Buy
    buy_msg.client_id = ClientId{1};
    buy_msg.order_id = OrderId{1};
    buy_msg.price = Price{100};
    buy_msg.quantity = Quantity{10};
    ASSERT_TRUE(send_binary_msg(fd_a, buy_msg));

    auto resp_a = recv_binary_msg(fd_a);
    ASSERT_TRUE(resp_a.has_value());
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(*resp_a));

    // Client B: aggressive sell at 100 for 10 qty — crosses A's buy.
    binary_protocol::LimitOrderAddMsg sell_msg;
    sell_msg.type = binary_protocol::MessageType::LimitOrderAdd;
    sell_msg.side = 1;  // Sell
    sell_msg.client_id = ClientId{2};
    sell_msg.order_id = OrderId{2};
    sell_msg.price = Price{100};
    sell_msg.quantity = Quantity{10};
    ASSERT_TRUE(send_binary_msg(fd_b, sell_msg));

    // B's crossing sell produces one frame carrying AckMsg (remaining_qty=0)
    // immediately followed by a TradeNotificationMsg.
    auto msgs_b = recv_binary_msgs(fd_b);
    ASSERT_EQ(msgs_b.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(msgs_b[0]));
    EXPECT_EQ(std::get<binary_protocol::AckMsg>(msgs_b[0]).remaining_qty,
              Quantity{0});
    ASSERT_TRUE(std::holds_alternative<binary_protocol::TradeNotificationMsg>(
        msgs_b[1]));
    auto& tn = std::get<binary_protocol::TradeNotificationMsg>(msgs_b[1]);
    EXPECT_EQ(tn.price, Price{100});
    EXPECT_EQ(tn.quantity, Quantity{10});

    ::close(fd_a);
    ::close(fd_b);
}

// Cancel a resting order → AckMsg.
TEST_F(BinaryExchangeServerE2ETest, CancelOrder) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    // Place a resting limit order.
    binary_protocol::LimitOrderAddMsg add_msg;
    add_msg.type = binary_protocol::MessageType::LimitOrderAdd;
    add_msg.side = 0;
    add_msg.client_id = ClientId{1};
    add_msg.order_id = OrderId{1};
    add_msg.price = Price{100};
    add_msg.quantity = Quantity{10};
    ASSERT_TRUE(send_binary_msg(fd, add_msg));

    auto add_resp = recv_binary_msg(fd);
    ASSERT_TRUE(add_resp.has_value());
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(*add_resp));

    // Cancel it.
    binary_protocol::CancelMsg cancel_msg;
    cancel_msg.type = binary_protocol::MessageType::Cancel;
    cancel_msg.padding = 0;
    cancel_msg.client_id = ClientId{1};
    cancel_msg.order_id = OrderId{1};
    ASSERT_TRUE(send_binary_msg(fd, cancel_msg));

    auto cancel_resp = recv_binary_msg(fd);
    ASSERT_TRUE(cancel_resp.has_value());
    // Cancel of accepted order → Accepted → AckMsg
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(*cancel_resp));

    ::close(fd);
}

// Malformed binary payload → RejectMsg with reason_code=6 (parse error).
TEST_F(BinaryExchangeServerE2ETest, MalformedPayload) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    // Send a frame with garbage payload (not a valid binary message).
    std::string garbage = "\xFF\x01\x02";
    ASSERT_TRUE(send_all(fd, make_frame(garbage)));

    auto response = recv_binary_msg(fd);
    ASSERT_TRUE(response.has_value()) << "Timed out waiting for error response";
    ASSERT_TRUE(std::holds_alternative<binary_protocol::RejectMsg>(*response));

    auto& reject = std::get<binary_protocol::RejectMsg>(*response);
    EXPECT_EQ(reject.reason_code, 6);  // parse error

    ::close(fd);
}

// Market order sweeps a resting limit.
TEST_F(BinaryExchangeServerE2ETest, MarketOrder) {
    int fd_a = connect_client();
    int fd_b = connect_client();
    ASSERT_GE(fd_a, 0);
    ASSERT_GE(fd_b, 0);

    // Client A: resting sell limit at 200 for 5 qty.
    binary_protocol::LimitOrderAddMsg sell_msg;
    sell_msg.type = binary_protocol::MessageType::LimitOrderAdd;
    sell_msg.side = 1;  // Sell
    sell_msg.client_id = ClientId{1};
    sell_msg.order_id = OrderId{1};
    sell_msg.price = Price{200};
    sell_msg.quantity = Quantity{5};
    ASSERT_TRUE(send_binary_msg(fd_a, sell_msg));

    auto resp_a = recv_binary_msg(fd_a);
    ASSERT_TRUE(resp_a.has_value());
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(*resp_a));

    // Client B: market buy for 5 qty.
    binary_protocol::MarketOrderAddMsg market_msg;
    market_msg.type = binary_protocol::MessageType::MarketOrderAdd;
    market_msg.side = 0;  // Buy
    market_msg.client_id = ClientId{2};
    market_msg.order_id = OrderId{2};
    market_msg.quantity = Quantity{5};
    ASSERT_TRUE(send_binary_msg(fd_b, market_msg));

    // B gets one frame carrying AckMsg + TradeNotificationMsg.
    auto msgs_b = recv_binary_msgs(fd_b);
    ASSERT_EQ(msgs_b.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<binary_protocol::AckMsg>(msgs_b[0]));
    EXPECT_EQ(std::get<binary_protocol::AckMsg>(msgs_b[0]).remaining_qty,
              Quantity{0});
    ASSERT_TRUE(std::holds_alternative<binary_protocol::TradeNotificationMsg>(
        msgs_b[1]));
    auto& tn = std::get<binary_protocol::TradeNotificationMsg>(msgs_b[1]);
    EXPECT_EQ(tn.price, Price{200});
    EXPECT_EQ(tn.quantity, Quantity{5});

    ::close(fd_a);
    ::close(fd_b);
}

}  // namespace
}  // namespace miniexchange

#endif  // __linux__
