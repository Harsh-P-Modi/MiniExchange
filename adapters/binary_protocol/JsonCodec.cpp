#include "adapters/binary_protocol/JsonCodec.hpp"

namespace miniexchange::binary_protocol {

// --- LimitOrderAddMsg ---

void to_json(nlohmann::json& j, const LimitOrderAddMsg& msg) {
    j = nlohmann::json{
        {"type", static_cast<uint8_t>(msg.type)},
        {"side", msg.side},
        {"client_id", msg.client_id.value},
        {"order_id", msg.order_id.value},
        {"price", msg.price.value},
        {"quantity", msg.quantity.value},
    };
}

void from_json(const nlohmann::json& j, LimitOrderAddMsg& msg) {
    msg.type = static_cast<MessageType>(j.at("type").get<uint8_t>());
    msg.side = j.at("side").get<uint8_t>();
    msg.client_id = ClientId{j.at("client_id").get<uint64_t>()};
    msg.order_id = OrderId{j.at("order_id").get<uint64_t>()};
    msg.price = Price{j.at("price").get<int64_t>()};
    msg.quantity = Quantity{j.at("quantity").get<uint64_t>()};
}

// --- MarketOrderAddMsg ---

void to_json(nlohmann::json& j, const MarketOrderAddMsg& msg) {
    j = nlohmann::json{
        {"type", static_cast<uint8_t>(msg.type)},
        {"side", msg.side},
        {"client_id", msg.client_id.value},
        {"order_id", msg.order_id.value},
        {"quantity", msg.quantity.value},
    };
}

void from_json(const nlohmann::json& j, MarketOrderAddMsg& msg) {
    msg.type = static_cast<MessageType>(j.at("type").get<uint8_t>());
    msg.side = j.at("side").get<uint8_t>();
    msg.client_id = ClientId{j.at("client_id").get<uint64_t>()};
    msg.order_id = OrderId{j.at("order_id").get<uint64_t>()};
    msg.quantity = Quantity{j.at("quantity").get<uint64_t>()};
}

// --- CancelMsg ---

void to_json(nlohmann::json& j, const CancelMsg& msg) {
    j = nlohmann::json{
        {"type", static_cast<uint8_t>(msg.type)},
        {"client_id", msg.client_id.value},
        {"order_id", msg.order_id.value},
    };
}

void from_json(const nlohmann::json& j, CancelMsg& msg) {
    msg.type = static_cast<MessageType>(j.at("type").get<uint8_t>());
    msg.padding = 0;
    msg.client_id = ClientId{j.at("client_id").get<uint64_t>()};
    msg.order_id = OrderId{j.at("order_id").get<uint64_t>()};
}

// --- AckMsg ---

void to_json(nlohmann::json& j, const AckMsg& msg) {
    j = nlohmann::json{
        {"type", static_cast<uint8_t>(msg.type)},
        {"order_id", msg.order_id.value},
        {"remaining_qty", msg.remaining_qty.value},
    };
}

void from_json(const nlohmann::json& j, AckMsg& msg) {
    msg.type = static_cast<MessageType>(j.at("type").get<uint8_t>());
    msg.padding = 0;
    msg.order_id = OrderId{j.at("order_id").get<uint64_t>()};
    msg.remaining_qty = Quantity{j.at("remaining_qty").get<uint64_t>()};
}

// --- RejectMsg ---

void to_json(nlohmann::json& j, const RejectMsg& msg) {
    j = nlohmann::json{
        {"type", static_cast<uint8_t>(msg.type)},
        {"reason_code", msg.reason_code},
        {"order_id", msg.order_id.value},
    };
}

void from_json(const nlohmann::json& j, RejectMsg& msg) {
    msg.type = static_cast<MessageType>(j.at("type").get<uint8_t>());
    msg.reason_code = j.at("reason_code").get<uint8_t>();
    msg.order_id = OrderId{j.at("order_id").get<uint64_t>()};
}

// --- TradeNotificationMsg ---

void to_json(nlohmann::json& j, const TradeNotificationMsg& msg) {
    j = nlohmann::json{
        {"type", static_cast<uint8_t>(msg.type)},
        {"buy_order_id", msg.buy_order_id.value},
        {"sell_order_id", msg.sell_order_id.value},
        {"price", msg.price.value},
        {"quantity", msg.quantity.value},
        {"trade_sequence", msg.trade_sequence.value},
    };
}

void from_json(const nlohmann::json& j, TradeNotificationMsg& msg) {
    msg.type = static_cast<MessageType>(j.at("type").get<uint8_t>());
    msg.padding = 0;
    msg.buy_order_id = OrderId{j.at("buy_order_id").get<uint64_t>()};
    msg.sell_order_id = OrderId{j.at("sell_order_id").get<uint64_t>()};
    msg.price = Price{j.at("price").get<int64_t>()};
    msg.quantity = Quantity{j.at("quantity").get<uint64_t>()};
    msg.trade_sequence = TradeSequence{j.at("trade_sequence").get<uint64_t>()};
}

}  // namespace miniexchange::binary_protocol
