#include <iostream>
#include <string>
#include <variant>

#include "apps/cli/cli_parser.hpp"
#include "apps/cli/console_printer.hpp"
#include "engine/matching_engine.hpp"
#include "interfaces/engine_api.hpp"

// main.cpp — the composition root for the CLI app.
//
// This is where the Ports & Adapters wiring happens:
//   CLIParser (reads stdin) → EngineAPI (the port) → ConsolePrinter (writes stdout)
//
// The engine is accessed exclusively through the EngineAPI* pointer,
// demonstrating the hexagonal architecture: this file could swap in a
// different EngineAPI implementation (e.g. a mock, or a Phase 3 pooled
// engine) without changing any parsing or printing code.
//
// Per requirements.md §7 and tasks.md Task 14, the CLI does NOT
// implement EventSink — EngineResponse alone suffices for a single-user
// CLI. The NullEventSink (default) is used inside the engine.

int main() {
    // Construct the engine with the default NullEventSink (no broadcast
    // observer needed for a single-user CLI).
    miniexchange::MatchingEngine engine;

    // Access the engine exclusively through the EngineAPI port.
    miniexchange::EngineAPI* api = &engine;

    // Construct parser and printer.
    miniexchange::cli::CLIParser parser;
    miniexchange::cli::ConsolePrinter printer(std::cout);

    std::string line;
    while (std::getline(std::cin, line)) {
        auto result = parser.parse(line);

        std::visit(
            [&](auto&& cmd) {
                using T = std::decay_t<decltype(cmd)>;

                if constexpr (std::is_same_v<T, miniexchange::LimitOrder>) {
                    auto response = api->submit(cmd);
                    printer.print_response(response);
                } else if constexpr (std::is_same_v<T,
                                                    miniexchange::MarketOrder>) {
                    auto response = api->submit(cmd);
                    printer.print_response(response);
                } else if constexpr (std::is_same_v<
                                         T, miniexchange::cli::CancelRequest>) {
                    auto response = api->cancel(cmd.id);
                    printer.print_cancel_response(response, cmd.id);
                } else if constexpr (std::is_same_v<
                                         T,
                                         miniexchange::cli::PrintBookRequest>) {
                    printer.print_book(api->book());
                } else if constexpr (std::is_same_v<
                                         T, miniexchange::cli::QuitRequest>) {
                    return;  // exits the lambda; loop terminates below
                } else if constexpr (std::is_same_v<
                                         T, miniexchange::cli::ParseError>) {
                    printer.print_error(cmd.message);
                }
            },
            result);

        // Check if QUIT was received — std::visit above can't break the
        // outer loop directly, so we check the variant after the visit.
        if (std::holds_alternative<miniexchange::cli::QuitRequest>(result)) {
            break;
        }
    }

    return 0;
}
