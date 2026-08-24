#ifndef MINIEXCHANGE_APPS_CLI_CONSOLE_PRINTER_HPP
#define MINIEXCHANGE_APPS_CLI_CONSOLE_PRINTER_HPP

#include <ostream>

#include "core/Events.hpp"
#include "core/Types.hpp"

namespace miniexchange {

// Forward declaration — avoid pulling in the full order_book.hpp header.
class OrderBook;

namespace cli {

// ConsolePrinter — renders engine results and book state to an ostream.
//
// This is pure presentation logic: it formats EngineResponse, rejection
// reasons, and book depth for human-readable CLI output. All I/O lives
// here — the engine, orderbook, and core never touch stdout.
//
// Per requirements.md §7, the CLI does NOT implement EventSink in
// Phase 1 — EngineResponse alone is sufficient for a single-user CLI.
// EventSink exists as a port so Phase 2/6 observers can subscribe later
// without changing engine code.
class ConsolePrinter {
public:
    // Constructs a printer that writes to the given output stream.
    // Defaults to std::cout if not specified.
    explicit ConsolePrinter(std::ostream& out);

    // Render an EngineResponse from a submit() call.
    // Shows status, any trades that occurred, and remaining quantity.
    void print_response(const EngineResponse& response) const;

    // Render an EngineResponse from a cancel() call.
    void print_cancel_response(const EngineResponse& response, OrderId id) const;

    // Render the full book depth (bids and asks).
    void print_book(const OrderBook& book) const;

    // Render a parse error message.
    void print_error(const std::string& message) const;

private:
    std::ostream& out_;

    // Helper: human-readable string for EngineResult.
    static const char* result_to_string(EngineResult result);

    // Helper: human-readable side string.
    static const char* side_to_string(Side side);
};

}  // namespace cli
}  // namespace miniexchange

#endif  // MINIEXCHANGE_APPS_CLI_CONSOLE_PRINTER_HPP
