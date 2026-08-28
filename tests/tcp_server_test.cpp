#ifdef __linux__

#include "adapters/tcp/tcp_server.hpp"

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace miniexchange::tcp {
namespace {

// Helper: encode a payload into a length-prefixed frame (4-byte BE + payload).
std::string make_frame(std::string_view payload) {
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::string frame(4 + len, '\0');
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >>  8) & 0xFF);
    frame[3] = static_cast<char>((len      ) & 0xFF);
    std::memcpy(frame.data() + 4, payload.data(), len);
    return frame;
}

// Captured frame: ClientId + payload string.
struct CapturedFrame {
    ClientId client_id;
    std::string payload;
};

// Test fixture: starts a TcpServer on an ephemeral port in a background
// thread, captures frames via the registered handler, and tears down
// cleanly after each test.
class TcpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<TcpServer>(0);  // OS-assigned port
        server_->set_frame_handler(
            [this](ClientId id, std::string_view payload) {
                std::lock_guard<std::mutex> lock(mutex_);
                frames_.push_back({id, std::string(payload)});
            });
        server_thread_ = std::thread([this] { server_->run(); });
        // Brief delay for the server thread to enter epoll_wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void TearDown() override {
        server_->request_shutdown();
        // Wake epoll_wait by connecting a dummy client (the server
        // checks the shutdown flag on every epoll wakeup).
        wake_server();
        if (server_thread_.joinable()) {
            server_thread_.join();
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

    // Send raw bytes over a connected fd.
    bool send_all(int fd, const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            ssize_t n = ::send(fd, data.data() + sent,
                               data.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Wait until at least `count` frames are captured, or timeout.
    bool wait_for_frames(std::size_t count,
                         std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (frames_.size() >= count) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    // Wake epoll_wait so the server can notice the shutdown flag.
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

    std::unique_ptr<TcpServer> server_;
    std::thread server_thread_;
    std::mutex mutex_;
    std::vector<CapturedFrame> frames_;
};

// --- Test: Partial frame reassembly ---
// Send the 4-byte length prefix in one write and the payload in a
// separate write. The server must buffer partial data and deliver
// one complete frame once both parts arrive.
TEST_F(TcpServerTest, PartialFrameReassembly) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    std::string payload = "HELLO_PARTIAL";
    std::string frame = make_frame(payload);

    // Split: send length prefix first, then payload.
    ASSERT_TRUE(send_all(fd, frame.substr(0, 4)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(send_all(fd, frame.substr(4)));

    ASSERT_TRUE(wait_for_frames(1));

    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_EQ(frames_[0].payload, payload);

    ::close(fd);
}

// --- Test: Two frames concatenated in one TCP segment ---
// Send two complete length-prefixed frames in a single write().
// The server must parse both as separate frames.
TEST_F(TcpServerTest, TwoFramesInOneSegment) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    std::string payload1 = "FRAME_ONE";
    std::string payload2 = "FRAME_TWO";
    std::string combined = make_frame(payload1) + make_frame(payload2);

    ASSERT_TRUE(send_all(fd, combined));

    ASSERT_TRUE(wait_for_frames(2));

    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT_EQ(frames_.size(), 2u);
    EXPECT_EQ(frames_[0].payload, payload1);
    EXPECT_EQ(frames_[1].payload, payload2);

    ::close(fd);
}

// --- Test: Multiple concurrent clients are independent ---
// Connect 3 clients, each sends a distinct frame. Verify all 3
// arrive with distinct ClientIds — no cross-talk.
TEST_F(TcpServerTest, MultipleClientsIndependent) {
    int fd1 = connect_client();
    int fd2 = connect_client();
    int fd3 = connect_client();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);
    ASSERT_GE(fd3, 0);

    ASSERT_TRUE(send_all(fd1, make_frame("CLIENT_1")));
    ASSERT_TRUE(send_all(fd2, make_frame("CLIENT_2")));
    ASSERT_TRUE(send_all(fd3, make_frame("CLIENT_3")));

    ASSERT_TRUE(wait_for_frames(3));

    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT_EQ(frames_.size(), 3u);

    // Collect payloads and ClientIds.
    std::vector<std::string> payloads;
    std::vector<ClientId> ids;
    for (const auto& f : frames_) {
        payloads.push_back(f.payload);
        ids.push_back(f.client_id);
    }

    // All three distinct payloads must be present (order may vary).
    EXPECT_NE(std::find(payloads.begin(), payloads.end(), "CLIENT_1"),
              payloads.end());
    EXPECT_NE(std::find(payloads.begin(), payloads.end(), "CLIENT_2"),
              payloads.end());
    EXPECT_NE(std::find(payloads.begin(), payloads.end(), "CLIENT_3"),
              payloads.end());

    // All three ClientIds must be distinct.
    EXPECT_NE(ids[0], ids[1]);
    EXPECT_NE(ids[0], ids[2]);
    EXPECT_NE(ids[1], ids[2]);

    ::close(fd1);
    ::close(fd2);
    ::close(fd3);
}

// --- Test: Oversized frame disconnects the offending client ---
// A frame whose length prefix exceeds kMaxFrameSize causes that
// client to be disconnected. Other clients remain unaffected (NFR2).
TEST_F(TcpServerTest, OversizedFrameDisconnectsClient) {
    int bad_fd = connect_client();
    int good_fd = connect_client();
    ASSERT_GE(bad_fd, 0);
    ASSERT_GE(good_fd, 0);

    // Send a frame header claiming a payload larger than kMaxFrameSize.
    uint32_t oversized_len = kMaxFrameSize + 1;
    char header[4];
    header[0] = static_cast<char>((oversized_len >> 24) & 0xFF);
    header[1] = static_cast<char>((oversized_len >> 16) & 0xFF);
    header[2] = static_cast<char>((oversized_len >>  8) & 0xFF);
    header[3] = static_cast<char>((oversized_len      ) & 0xFF);
    ASSERT_TRUE(send_all(bad_fd, std::string(header, 4)));

    // Give the server time to process and disconnect the bad client.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The bad client should be disconnected: further writes fail or
    // reads return 0 (EOF). Try reading — should get 0 or error.
    char buf[1];
    ssize_t n = ::recv(bad_fd, buf, sizeof(buf), MSG_DONTWAIT);
    // n == 0 means EOF (peer closed), n < 0 with ECONNRESET also valid.
    EXPECT_TRUE(n == 0 || (n < 0 && errno == ECONNRESET));

    // The good client should still be connected: send a frame and
    // verify it arrives.
    ASSERT_TRUE(send_all(good_fd, make_frame("STILL_ALIVE")));
    ASSERT_TRUE(wait_for_frames(1));

    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT_EQ(frames_.size(), 1u);
    EXPECT_EQ(frames_[0].payload, "STILL_ALIVE");

    ::close(bad_fd);
    ::close(good_fd);
}

// --- Test: Edge-triggered drain (data arriving during processing) ---
// With edge-triggered epoll, the server must drain the entire socket
// buffer in one read loop. Send a burst of frames rapidly and verify
// all are received — none silently lost because epoll only notified
// once.
TEST_F(TcpServerTest, EdgeTriggeredDrain) {
    int fd = connect_client();
    ASSERT_GE(fd, 0);

    constexpr int kBurstCount = 50;
    std::string burst;
    for (int i = 0; i < kBurstCount; ++i) {
        burst += make_frame("BURST_" + std::to_string(i));
    }

    // Send the entire burst as one write — the kernel may deliver it
    // as a single notification to the edge-triggered epoll fd.
    ASSERT_TRUE(send_all(fd, burst));

    ASSERT_TRUE(wait_for_frames(kBurstCount));

    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT_EQ(frames_.size(), static_cast<std::size_t>(kBurstCount));
    for (int i = 0; i < kBurstCount; ++i) {
        EXPECT_EQ(frames_[static_cast<std::size_t>(i)].payload,
                  "BURST_" + std::to_string(i));
    }

    ::close(fd);
}

// --- Test: Slow reader does not block other clients (NFR2) ---
// One client accumulates large unsent data (simulating a slow reader
// that never recv()s), while another client continues sending frames
// and having them processed normally. The slow reader must not stall
// the server's ability to service other clients.
//
// Strategy: register a frame handler that echoes large responses back
// to the sending client via send_to_client (called from the I/O
// thread context, which is safe). The "slow" client never reads its
// socket, so its kernel buffer fills up; the server arms EPOLLOUT and
// handles EAGAIN. Meanwhile the "fast" client's frames must still be
// processed without delay.
TEST_F(TcpServerTest, SlowReaderDoesNotBlockOthers) {
    // Override the frame handler: if the payload starts with "FLOOD_",
    // echo a large response back to the sender (flooding their write
    // buffer). Otherwise capture normally.
    server_->set_frame_handler(
        [this](ClientId id, std::string_view payload) {
            if (payload.size() >= 6 && payload.substr(0, 6) == "FLOOD_") {
                // Send a large response to the client — they won't
                // read it, so the kernel buffer fills up.
                std::string large_response(2048, 'X');
                server_->send_to_client(id, large_response);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            frames_.push_back({id, std::string(payload)});
        });

    int slow_fd = connect_client();
    int fast_fd = connect_client();
    ASSERT_GE(slow_fd, 0);
    ASSERT_GE(fast_fd, 0);

    // Flood the slow client's outbound buffer: send many FLOOD_ frames
    // from the slow client. The handler echoes large data back, which
    // the slow_fd never reads — eventually EAGAIN on the write side.
    for (int i = 0; i < 100; ++i) {
        std::string msg = "FLOOD_" + std::to_string(i);
        if (!send_all(slow_fd, make_frame(msg))) break;
    }

    // Give the server time to process the flood and hit EAGAIN/EPOLLOUT.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // The fast client should still be serviced promptly.
    ASSERT_TRUE(send_all(fast_fd, make_frame("FAST_MSG")));

    // Wait for the fast client's frame (allow extra time since the
    // server is busy processing the flood).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    bool found_fast = false;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& f : frames_) {
                if (f.payload == "FAST_MSG") {
                    found_fast = true;
                    break;
                }
            }
        }
        if (found_fast) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(found_fast)
        << "Fast client's frame was not delivered — slow reader blocked the server";

    ::close(slow_fd);
    ::close(fast_fd);
}

}  // namespace
}  // namespace miniexchange::tcp

#else
// Non-Linux: produce a single passing test so the binary is valid.
#include <gtest/gtest.h>
TEST(TcpServerTest, SkippedOnNonLinux) {
    GTEST_SKIP() << "TCP server tests require Linux (epoll)";
}
#endif  // __linux__
