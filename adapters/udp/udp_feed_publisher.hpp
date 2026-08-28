#ifndef MINIEXCHANGE_ADAPTERS_UDP_UDP_FEED_PUBLISHER_HPP
#define MINIEXCHANGE_ADAPTERS_UDP_UDP_FEED_PUBLISHER_HPP

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "adapters/udp/FeedMessage.hpp"
#include "adapters/udp/TopOfBook.hpp"
#include "core/Events.hpp"
#include "core/Trade.hpp"
#include "core/Types.hpp"
#include "interfaces/event_sink.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using ssize_t = int64_t;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

namespace miniexchange::udp {

// SendFunction — injectable send abstraction for testing.
// In production: wraps sendto() with MSG_DONTWAIT.
// In tests: a lambda that captures sent bytes for assertion.
// Returns number of bytes sent, or -1 on failure (simulates EWOULDBLOCK).
using SendFunction = std::function<ssize_t(int fd, const void* data,
                                           std::size_t len,
                                           const struct sockaddr* addr,
                                           socklen_t addrlen)>;

// Subscriber — address of a single unicast fan-out target.
struct Subscriber {
    struct sockaddr_in addr;
};

// UdpFeedPublisher — implements EventSink, publishes top-of-book updates
// and trade prints over UDP.
//
// Designed per design.md §1, §1a, §1b, §4:
// - Derives top-of-book state from EventSink callbacks alone (no OrderBook access)
// - Tracks qty (published aggregate) and count (internal order count) at best
// - Publishes TopOfBookMessage only when top-of-book changes
// - Drops on EWOULDBLOCK (no retry, no queue) per §4
// - Emits SnapshotMessage every N messages per §5
//
// Construction: takes a fixed SymbolId, subscriber list, socket fd,
// and optionally a SendFunction (defaults to real sendto on Linux).
class UdpFeedPublisher final : public EventSink {
public:
    // snapshot_interval: emit a SnapshotMessage every N messages sent.
    // Default 500 per design.md §5.
    explicit UdpFeedPublisher(SymbolId symbol,
                              std::vector<Subscriber> subscribers,
                              int socket_fd,
                              uint64_t snapshot_interval = 500,
                              SendFunction send_fn = nullptr);

    ~UdpFeedPublisher() override = default;

    // EventSink interface
    void on_trade(const Trade& trade) override;
    void on_order_accepted(const OrderAccepted& event) override;
    void on_order_cancelled(const OrderCancelled& event) override;

    // Read-only accessors for testing
    [[nodiscard]] TopOfBook current_top_of_book() const;
    [[nodiscard]] uint64_t current_sequence() const { return next_sequence_ - 1; }
    [[nodiscard]] uint64_t messages_since_snapshot() const { return messages_since_snapshot_; }

private:
    // Internal per-side state at the tracked best price
    struct SideState {
        Price    price{};      // current best price (Price{0} = drained/unknown)
        Quantity qty{};        // aggregate resting qty at best price
        uint64_t count = 0;   // number of distinct orders at best price
    };

    // Send a message to all subscribers. Returns true if at least one
    // subscriber received it (false = all dropped due to EWOULDBLOCK).
    template <typename Msg>
    bool send_to_all(const Msg& msg);

    // Build and send a TopOfBookMessage reflecting current state.
    void publish_top_of_book();

    // Build and send a SnapshotMessage reflecting current state.
    void publish_snapshot();

    // Populate the FeedHeader for a new message.
    FeedHeader make_header(MessageType type);

    // Check and emit snapshot if message count threshold reached.
    void check_snapshot_trigger();

    // Get current CLOCK_MONOTONIC in nanoseconds (0 on non-Linux).
    static uint64_t now_ns();

    SymbolId symbol_;
    std::vector<Subscriber> subscribers_;
    int socket_fd_;
    uint64_t snapshot_interval_;
    SendFunction send_fn_;

    // Feed-level sequence counter (§3): incremented once per message sent.
    uint64_t next_sequence_ = 1;

    // Snapshot cadence counter (§5)
    uint64_t messages_since_snapshot_ = 0;

    // Per-side top-of-book state (§1b)
    SideState bid_;
    SideState ask_;
};

}  // namespace miniexchange::udp

#endif  // MINIEXCHANGE_ADAPTERS_UDP_UDP_FEED_PUBLISHER_HPP
