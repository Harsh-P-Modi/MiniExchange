#include "adapters/fix/FixEncoder.hpp"

#include <ctime>
#include <numeric>
#include <string>

#include "adapters/fix/FixMessage.hpp"
#include "adapters/fix/FixParser.hpp"  // for kSupportedSymbol

namespace miniexchange::fix {

namespace {

// Append "tag=value<SOH>" to out.
void append_field(std::string& out, int tag, std::string_view value) {
    out += std::to_string(tag);
    out += '=';
    out += value;
    out += kSOH;
}

void append_field(std::string& out, int tag, uint64_t value) {
    append_field(out, tag, std::to_string(value));
}

// UTC timestamp in FIX format: YYYYMMDD-HH:MM:SS.
std::string sending_time_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H:%M:%S", &tm_utc);
    return std::string(buf);
}

// Sum of filled quantity across all trades in the response.
uint64_t total_filled(const EngineResponse& response) {
    uint64_t filled = 0;
    for (const auto& t : response.trades) {
        filled += t.quantity.value;
    }
    return filled;
}

}  // namespace

char ord_status_for(const EngineResponse& response, Quantity original_qty,
                    bool is_cancel) {
    // Map engine outcome -> FIX OrdStatus (tag 39):
    //   0 New, 1 PartiallyFilled, 2 Filled, 4 Cancelled, 8 Rejected.
    if (response.status != EngineResult::Accepted) {
        // Any non-Accepted result (DuplicateOrderId, UnknownOrderId,
        // InvalidPrice, risk rejections, STP, ...) is a rejection from
        // the client's point of view.
        return '8';  // Rejected
    }
    if (is_cancel) {
        // An accepted cancel means the resting order was removed.
        return '4';  // Cancelled
    }
    const uint64_t filled = total_filled(response);
    if (filled == 0) {
        return '0';  // New (accepted, resting, nothing filled yet)
    }
    if (filled >= original_qty.value) {
        return '2';  // Filled
    }
    return '1';  // PartiallyFilled
}

std::string FixEncoder::frame(const std::string& body) {
    // body already begins at "35=..." and ends with the SOH after the
    // last business field. Compute BodyLength = body.size() (bytes from
    // just after 9=...<SOH> up to and including the SOH before 10=).
    std::string header;
    append_field(header, 8, kBeginString);
    append_field(header, 9, static_cast<uint64_t>(body.size()));

    std::string without_checksum = header + body;
    uint8_t cksum = FixMessage::compute_checksum(without_checksum);

    // CheckSum is always exactly 3 digits, zero-padded (FIX spec).
    char cbuf[4];
    std::snprintf(cbuf, sizeof(cbuf), "%03u", static_cast<unsigned>(cksum));
    std::string out = without_checksum;
    append_field(out, 10, std::string_view(cbuf, 3));
    return out;
}

std::string FixEncoder::encode_execution_report(const ExecReportContext& ctx,
                                                const EngineResponse& response) {
    std::string body;

    // MsgType first field of the body.
    append_field(body, 35, std::string_view("8"));

    // Session header tags (FIX 4.2): SenderCompID, TargetCompID,
    // MsgSeqNum, SendingTime.
    append_field(body, 49, sender_comp_id_);
    append_field(body, 56, target_comp_id_);
    append_field(body, 34, next_seq_num_++);
    append_field(body, 52, sending_time_now());

    // Business tags.
    const char ord_status =
        ord_status_for(response, ctx.original_qty, ctx.is_cancel);

    append_field(body, 37, ctx.order_id.value);       // OrderID
    append_field(body, 11, ctx.order_id.value);       // ClOrdID (== OrderID, Q1)
    append_field(body, 17, next_exec_id_++);          // ExecID
    append_field(body, 39, std::string_view(&ord_status, 1));   // OrdStatus
    append_field(body, 150, std::string_view(&ord_status, 1));  // ExecType (mirrors, minimal scope)

    // Side (tag 54): 1=Buy, 2=Sell.
    append_field(body, 54,
                 std::string_view(ctx.side == Side::Buy ? "1" : "2"));
    append_field(body, 55, kSupportedSymbol);  // Symbol echo

    // Quantities: CumQty (14) filled so far, LeavesQty (151) remaining.
    const uint64_t filled = total_filled(response);
    append_field(body, 14, filled);                       // CumQty
    append_field(body, 151, response.remaining_qty.value);  // LeavesQty

    return frame(body);
}

}  // namespace miniexchange::fix
