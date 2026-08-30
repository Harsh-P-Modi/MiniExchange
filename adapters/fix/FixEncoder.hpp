#ifndef MINIEXCHANGE_ADAPTERS_FIX_FIX_ENCODER_HPP
#define MINIEXCHANGE_ADAPTERS_FIX_FIX_ENCODER_HPP

#include <cstdint>
#include <string>

#include "core/Events.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"

namespace miniexchange::fix {

// Context describing the order an ExecutionReport is about. EngineResponse
// alone doesn't carry the originating order's id/side/symbol for a resting
// or rejected order (its `trades` may be empty), so the caller supplies
// the originating order's identity. This mirrors how a real gateway keeps
// the ClOrdID/side of the order it forwarded.
struct ExecReportContext {
    OrderId order_id;
    Side side;
    Quantity original_qty;  // the quantity originally submitted
    bool is_cancel = false;  // true when reporting the outcome of a
                             // cancel() rather than a submit(): an
                             // Accepted cancel maps to OrdStatus=4
                             // (Cancelled), not 0 (New).
};

// FixEncoder — builds 35=8 (ExecutionReport) messages from engine results
// (design.md §4). One encoder instance == one FIX session: it owns the
// per-session MsgSeqNum (tag 34) and ExecID (tag 17) counters.
class FixEncoder {
public:
    FixEncoder(std::string sender_comp_id, std::string target_comp_id)
        : sender_comp_id_(std::move(sender_comp_id)),
          target_comp_id_(std::move(target_comp_id)) {}

    // Encode an ExecutionReport for the outcome of a submit()/cancel().
    // `ctx` supplies the originating order identity; `response` supplies
    // the outcome (status + any fills + remaining qty).
    std::string encode_execution_report(const ExecReportContext& ctx,
                                        const EngineResponse& response);

private:
    std::string sender_comp_id_;
    std::string target_comp_id_;
    uint64_t next_seq_num_ = 1;
    uint64_t next_exec_id_ = 1;

    // Assemble a full FIX message from an already-built body (the fields
    // between BodyLength and CheckSum, i.e. starting at "35="). Prepends
    // 8=/9= and appends the computed 10=. Shared, message-type-agnostic.
    std::string frame(const std::string& body);
};

// OrdStatus (tag 39) / ExecType (tag 150) derived from an EngineResponse.
// Exposed for testing the mapping table independently of the full encode.
char ord_status_for(const EngineResponse& response, Quantity original_qty,
                    bool is_cancel);

}  // namespace miniexchange::fix

#endif  // MINIEXCHANGE_ADAPTERS_FIX_FIX_ENCODER_HPP
