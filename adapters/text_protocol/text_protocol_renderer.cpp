#include "adapters/text_protocol/text_protocol_renderer.hpp"

#include <string>

namespace miniexchange::text_protocol {

namespace {

// Phase 11 R2: tag both ACCEPTED and REJECTED lines with the OrderId the
// response answers, so a client that pipelines several orders can
// correlate each reply to its request. Additive to the plaintext debug
// format — existing substring checks for "ACCEPTED" / "REJECTED" are
// unaffected. `id=0` means the engine could not attribute the response
// to a specific order (should not happen post-Phase-11 for submit/cancel).
std::string id_tag(const EngineResponse& response) {
    return " id=" + std::to_string(response.order_id.value);
}

const char* result_to_string(EngineResult result) {
    switch (result) {
        case EngineResult::Accepted:
            return "Accepted";
        case EngineResult::DuplicateOrderId:
            return "DuplicateOrderId";
        case EngineResult::UnknownOrderId:
            return "UnknownOrderId";
        case EngineResult::InvalidQuantity:
            return "InvalidQuantity";
        case EngineResult::InvalidPrice:
            return "InvalidPrice";
        case EngineResult::PoolExhausted:
            return "PoolExhausted";
        case EngineResult::SelfTradePrevented:
            return "SelfTradePrevented";
        case EngineResult::PriceOutOfBand:
            return "PriceOutOfBand";
        case EngineResult::QuantityTooLarge:
            return "QuantityTooLarge";
        case EngineResult::TickSizeMisaligned:
            return "TickSizeMisaligned";
        case EngineResult::InternalError:
            return "InternalError";
    }
    return "Unknown";
}

}  // namespace

std::string render(const EngineResponse& response) {
    if (response.status != EngineResult::Accepted) {
        std::string out = "REJECTED";
        out += id_tag(response);
        out += ": ";
        out += result_to_string(response.status);
        out += '\n';
        return out;
    }

    // Accepted — render fills if any occurred.
    std::string out = "ACCEPTED";
    out += id_tag(response);
    out += ":";

    if (response.trades.empty()) {
        out += " no fills, remaining_qty=";
        out += std::to_string(response.remaining_qty.value);
    } else {
        for (const auto& trade : response.trades) {
            out += " FILL ";
            out += std::to_string(trade.quantity.value);
            out += '@';
            out += std::to_string(trade.price.value);
        }
        if (response.remaining_qty.value > 0) {
            out += " | remaining_qty=";
            out += std::to_string(response.remaining_qty.value);
        } else {
            out += " | FULLY FILLED";
        }
    }

    out += '\n';
    return out;
}

std::string render_error(const std::string& message) {
    std::string out = "ERROR: ";
    out += message;
    out += '\n';
    return out;
}

}  // namespace miniexchange::text_protocol
