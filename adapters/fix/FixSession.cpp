#include "adapters/fix/FixSession.hpp"

#include <variant>

#include "adapters/fix/FixMessage.hpp"
#include "core/EngineCommand.hpp"

namespace miniexchange::fix {

namespace {

// Extract the (order_id, side, quantity) identity from a NewOrder variant
// for building the ExecutionReport context.
ExecReportContext context_from_new_order(const NewOrder& order) {
    return std::visit(
        [](const auto& o) -> ExecReportContext {
            return ExecReportContext{o.id, o.side, o.quantity,
                                     /*is_cancel=*/false};
        },
        order);
}

}  // namespace

std::variant<std::string, FixError> FixSession::handle_message(
    std::string_view raw) {
    // 1. Validate envelope (checksum + bodylength) and tokenize.
    auto validated = FixMessage::parse_validated(raw);
    if (std::holds_alternative<FixError>(validated)) {
        return std::get<FixError>(validated);
    }
    const TagMap& map = std::get<TagMap>(validated);

    // 2. Parse the business message into a NewOrder or CancelRequest.
    auto parsed = FixParser::parse(map, client_id_);
    if (std::holds_alternative<FixError>(parsed)) {
        return std::get<FixError>(parsed);
    }
    const ParsedMessage& msg = std::get<ParsedMessage>(parsed);

    // 3. Submit/cancel through the EngineAPI and encode the result.
    return std::visit(
        [this](const auto& command) -> std::variant<std::string, FixError> {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, NewOrder>) {
                ExecReportContext ctx = context_from_new_order(command);
                EngineResponse resp = engine_.submit(command);
                return encoder_.encode_execution_report(ctx, resp);
            } else {
                static_assert(std::is_same_v<T, CancelRequest>);
                // For a cancel we don't know the original side/qty from
                // the cancel message alone. Report side=Buy/qty=0 as
                // placeholders; OrdStatus is driven by is_cancel + the
                // engine result (Cancelled on success, Rejected on
                // UnknownOrderId), which is what a cancel-ack cares about.
                EngineResponse resp = engine_.cancel(command.id);
                ExecReportContext ctx{command.id, Side::Buy, Quantity{0},
                                      /*is_cancel=*/true};
                return encoder_.encode_execution_report(ctx, resp);
            }
        },
        msg);
}

}  // namespace miniexchange::fix
