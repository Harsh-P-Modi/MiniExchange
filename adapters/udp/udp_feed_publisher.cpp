#include "adapters/udp/udp_feed_publisher.hpp"

#include <cstring>

#ifdef __linux__
#include <time.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace miniexchange::udp {

namespace {

// Default send function: real sendto with MSG_DONTWAIT on Linux.
// On non-Linux (Windows build for tests): always returns the message size
// (simulating successful send) since we can't use real UDP sockets there.
ssize_t default_send(int fd, const void* data, std::size_t len,
                     const struct sockaddr* addr, socklen_t addrlen) {
#ifdef __linux__
    return ::sendto(fd, data, len, MSG_DONTWAIT, addr, addrlen);
#else
    (void)fd; (void)data; (void)addr; (void)addrlen;
    return static_cast<ssize_t>(len);  // pretend success on non-Linux
#endif
}

}  // namespace

UdpFeedPublisher::UdpFeedPublisher(SymbolId symbol,
                                   std::vector<Subscriber> subscribers,
                                   int socket_fd,
                                   uint64_t snapshot_interval,
                                   SendFunction send_fn)
    : symbol_(symbol),
      subscribers_(std::move(subscribers)),
      socket_fd_(socket_fd),
      snapshot_interval_(snapshot_interval),
      send_fn_(send_fn ? std::move(send_fn) : default_send) {}

// ─────────────────────────────────────────────────────────────────────────────
// EventSink: on_trade
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedPublisher::on_trade(const Trade& trade) {
    // 1. Build and send TradeMessage
    TradeMessage msg{};
    msg.header = make_header(MessageType::Trade);
    msg.symbol = symbol_;
    msg.price = trade.price;
    msg.quantity = trade.quantity;
    msg.trade_sequence = trade.trade_sequence;
    send_to_all(msg);
    check_snapshot_trigger();

    // 2. Update top-of-book state (§1b)
    // Determine which side the resting order was on: if the trade's
    // buy_order_id is the aggressor's, then the resting order was a sell
    // (and vice versa). But we don't know who the aggressor is from Trade
    // alone. Instead, we check which side's current best price matches
    // the trade price — that's the side that was consumed.
    //
    // If both sides have the same best price (crossed book shouldn't happen
    // in a correctly-functioning engine, but defensive): check both.
    bool updated = false;

    if (bid_.price == trade.price && bid_.count > 0) {
        // Trade consumed from the bid side
        bid_.qty = (trade.quantity.value <= bid_.qty.value)
                       ? Quantity{bid_.qty.value - trade.quantity.value}
                       : Quantity{0};
        if (trade.resting_order_removed) {
            bid_.count--;
        }
        if (bid_.count == 0) {
            bid_.price = Price{0};
            bid_.qty = Quantity{0};
        }
        updated = true;
    } else if (ask_.price == trade.price && ask_.count > 0) {
        // Trade consumed from the ask side
        ask_.qty = (trade.quantity.value <= ask_.qty.value)
                       ? Quantity{ask_.qty.value - trade.quantity.value}
                       : Quantity{0};
        if (trade.resting_order_removed) {
            ask_.count--;
        }
        if (ask_.count == 0) {
            ask_.price = Price{0};
            ask_.qty = Quantity{0};
        }
        updated = true;
    }

    if (updated) {
        publish_top_of_book();
        check_snapshot_trigger();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// EventSink: on_order_accepted
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedPublisher::on_order_accepted(const OrderAccepted& event) {
    // Market orders have Price{0} — they never affect top-of-book directly
    // (their fills are handled via on_trade).
    if (event.price == Price{0}) {
        return;
    }

    bool updated = false;

    if (event.side == Side::Buy) {
        // If this buy price would cross the current best ask, it will match
        // immediately and not rest — don't update the bid.
        if (ask_.count > 0 && event.price >= ask_.price) {
            return;
        }

        if (bid_.count == 0 || event.price > bid_.price) {
            // New best bid (better price or first order on this side)
            bid_.price = event.price;
            bid_.qty = event.quantity;
            bid_.count = 1;
            updated = true;
        } else if (event.price == bid_.price) {
            // Same level — add to aggregate
            bid_.qty += event.quantity;
            bid_.count++;
            updated = true;
        }
        // Worse price: no top-of-book change
    } else {
        // If this sell price would cross the current best bid, it will match
        // immediately and not rest — don't update the ask.
        if (bid_.count > 0 && event.price <= bid_.price) {
            return;
        }

        if (ask_.count == 0 || event.price < ask_.price) {
            // New best ask (better price or first order on this side)
            ask_.price = event.price;
            ask_.qty = event.quantity;
            ask_.count = 1;
            updated = true;
        } else if (event.price == ask_.price) {
            // Same level — add to aggregate
            ask_.qty += event.quantity;
            ask_.count++;
            updated = true;
        }
        // Worse price: no top-of-book change
    }

    if (updated) {
        publish_top_of_book();
        check_snapshot_trigger();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// EventSink: on_order_cancelled
// ─────────────────────────────────────────────────────────────────────────────

void UdpFeedPublisher::on_order_cancelled(const OrderCancelled& event) {
    bool updated = false;

    if (event.side == Side::Buy && event.price == bid_.price && bid_.count > 0) {
        bid_.qty = (event.remaining_qty.value <= bid_.qty.value)
                       ? Quantity{bid_.qty.value - event.remaining_qty.value}
                       : Quantity{0};
        bid_.count--;
        if (bid_.count == 0) {
            bid_.price = Price{0};
            bid_.qty = Quantity{0};
        }
        updated = true;
    } else if (event.side == Side::Sell && event.price == ask_.price && ask_.count > 0) {
        ask_.qty = (event.remaining_qty.value <= ask_.qty.value)
                       ? Quantity{ask_.qty.value - event.remaining_qty.value}
                       : Quantity{0};
        ask_.count--;
        if (ask_.count == 0) {
            ask_.price = Price{0};
            ask_.qty = Quantity{0};
        }
        updated = true;
    }

    if (updated) {
        publish_top_of_book();
        check_snapshot_trigger();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

TopOfBook UdpFeedPublisher::current_top_of_book() const {
    return TopOfBook{bid_.price, bid_.qty, ask_.price, ask_.qty};
}

FeedHeader UdpFeedPublisher::make_header(MessageType type) {
    FeedHeader h{};
    h.type = type;
    std::memset(h._pad, 0, sizeof(h._pad));
    h.sequence = next_sequence_++;
    h.timestamp_ns = now_ns();
    return h;
}

void UdpFeedPublisher::publish_top_of_book() {
    TopOfBookMessage msg{};
    msg.header = make_header(MessageType::TopOfBook);
    msg.symbol = symbol_;
    msg.bid_price = bid_.price;
    msg.bid_qty = bid_.qty;
    msg.ask_price = ask_.price;
    msg.ask_qty = ask_.qty;
    send_to_all(msg);
}

void UdpFeedPublisher::publish_snapshot() {
    SnapshotMessage msg{};
    msg.header = make_header(MessageType::Snapshot);
    msg.symbol = symbol_;
    msg.bid_price = bid_.price;
    msg.bid_qty = bid_.qty;
    msg.ask_price = ask_.price;
    msg.ask_qty = ask_.qty;
    msg.as_of_sequence = msg.header.sequence;  // snapshot IS the current sequence
    send_to_all(msg);
}

void UdpFeedPublisher::check_snapshot_trigger() {
    messages_since_snapshot_++;
    if (messages_since_snapshot_ >= snapshot_interval_) {
        publish_snapshot();
        messages_since_snapshot_ = 0;
    }
}

template <typename Msg>
bool UdpFeedPublisher::send_to_all(const Msg& msg) {
    bool any_sent = false;
    for (const auto& sub : subscribers_) {
        ssize_t result = send_fn_(
            socket_fd_,
            &msg,
            sizeof(msg),
            reinterpret_cast<const struct sockaddr*>(&sub.addr),
            sizeof(sub.addr));
        if (result > 0) {
            any_sent = true;
        }
        // On EWOULDBLOCK (result == -1): drop silently per design.md §4
    }
    return any_sent;
}

uint64_t UdpFeedPublisher::now_ns() {
#ifdef __linux__
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
#else
    return 0;  // Non-Linux: timestamps not meaningful in tests
#endif
}

// Explicit template instantiations for the three message types
// (needed because send_to_all is defined in the .cpp, not the header).
template bool UdpFeedPublisher::send_to_all(const TopOfBookMessage&);
template bool UdpFeedPublisher::send_to_all(const TradeMessage&);
template bool UdpFeedPublisher::send_to_all(const SnapshotMessage&);

}  // namespace miniexchange::udp
