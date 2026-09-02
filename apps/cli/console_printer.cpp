#include "apps/cli/console_printer.hpp"

#include <iostream>

#include "orderbook/order_book.hpp"

namespace miniexchange::cli {

ConsolePrinter::ConsolePrinter(std::ostream& out) : out_(out) {}

const char* ConsolePrinter::result_to_string(EngineResult result) {
    switch (result) {
        case EngineResult::Accepted:
            return "ACCEPTED";
        case EngineResult::DuplicateOrderId:
            return "REJECTED: DuplicateOrderId";
        case EngineResult::UnknownOrderId:
            return "REJECTED: UnknownOrderId";
        case EngineResult::InvalidQuantity:
            return "REJECTED: InvalidQuantity";
        case EngineResult::InvalidPrice:
            return "REJECTED: InvalidPrice";
        case EngineResult::PoolExhausted:
            return "REJECTED: PoolExhausted";
        case EngineResult::SelfTradePrevented:
            return "REJECTED: SelfTradePrevented";
        case EngineResult::PriceOutOfBand:
            return "REJECTED: PriceOutOfBand";
        case EngineResult::QuantityTooLarge:
            return "REJECTED: QuantityTooLarge";
        case EngineResult::TickSizeMisaligned:
            return "REJECTED: TickSizeMisaligned";
        case EngineResult::InternalError:
            return "REJECTED: InternalError";
    }
    return "UNKNOWN";
}

const char* ConsolePrinter::side_to_string(Side side) {
    switch (side) {
        case Side::Buy:
            return "BUY";
        case Side::Sell:
            return "SELL";
    }
    return "UNKNOWN";
}

void ConsolePrinter::print_response(const EngineResponse& response) const {
    if (response.status != EngineResult::Accepted) {
        out_ << result_to_string(response.status) << "\n";
        return;
    }

    // Accepted — show trades if any occurred.
    if (response.trades.empty()) {
        out_ << "ACCEPTED: no fills, remaining_qty="
             << response.remaining_qty.value << "\n";
    } else {
        out_ << "ACCEPTED:";
        for (const auto& trade : response.trades) {
            out_ << " FILL " << trade.quantity.value << "@"
                 << trade.price.value;
        }
        if (response.remaining_qty.value > 0) {
            out_ << " | remaining_qty=" << response.remaining_qty.value;
        } else {
            out_ << " | FULLY FILLED";
        }
        out_ << "\n";
    }
}

void ConsolePrinter::print_cancel_response(const EngineResponse& response,
                                           OrderId id) const {
    if (response.status == EngineResult::Accepted) {
        out_ << "CANCELLED: order " << id.value
             << ", remaining_qty=" << response.remaining_qty.value << "\n";
    } else {
        out_ << result_to_string(response.status) << "\n";
    }
}

void ConsolePrinter::print_book(const OrderBook& book) const {
    out_ << "=== ORDER BOOK ===\n";

    // Asks: printed in descending price order (highest at top, best ask
    // closest to the spread separator — standard visual order book layout).
    out_ << "--- ASKS ---\n";
    const auto& asks = book.asks();
    // The ask tree is sorted ascending (best ask = begin()), so reverse
    // iteration gives highest-price-first for display.
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        out_ << "  " << it->first.value << ": qty="
             << it->second.total_quantity().value << "\n";
    }

    out_ << "--- BIDS ---\n";
    const auto& bids = book.bids();
    // Bids are sorted descending (best = begin), so forward iteration
    // is already highest-price-first.
    for (const auto& [price, level] : bids) {
        out_ << "  " << price.value << ": qty="
             << level.total_quantity().value << "\n";
    }

    out_ << "==================\n";
}

void ConsolePrinter::print_error(const std::string& message) const {
    out_ << "ERROR: " << message << "\n";
}

}  // namespace miniexchange::cli
