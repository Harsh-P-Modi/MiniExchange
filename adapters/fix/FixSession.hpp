#ifndef MINIEXCHANGE_ADAPTERS_FIX_FIX_SESSION_HPP
#define MINIEXCHANGE_ADAPTERS_FIX_FIX_SESSION_HPP

#include <string>
#include <string_view>
#include <variant>

#include "adapters/fix/FixEncoder.hpp"
#include "adapters/fix/FixError.hpp"
#include "adapters/fix/FixParser.hpp"
#include "core/NewOrder.hpp"
#include "core/Types.hpp"
#include "interfaces/engine_api.hpp"

namespace miniexchange::fix {

// FixSession — the composition point (design.md §6): wires the parser and
// encoder to a live EngineAPI. This is written against EngineAPI, NOT
// MatchingEngine, so it automatically composes with Phase 8's RiskEngine
// decorator — a FIX order flows through risk checks exactly like a TCP
// order does.
//
// Session identity: a FIX session is identified by its counterparty's
// SenderCompID (tag 49). Phase 8 introduced ClientId as the engine-side
// owner used for self-trade prevention; a FIX session is a single
// persistent "client", so the session maps its peer's CompID to one fixed
// ClientId at construction and stamps it on every order it submits — the
// same role the TCP adapter's per-connection ClientId plays.
class FixSession {
public:
    // `engine` is the EngineAPI to submit through (a RiskEngine in the
    // full server, or a bare MatchingEngine in tests). `client_id` is the
    // owner stamped on this session's orders. sender/target CompIDs label
    // outbound ExecutionReports (note: on an outbound report WE are the
    // sender, so our_comp_id becomes tag 49 and peer_comp_id becomes 56).
    FixSession(EngineAPI& engine, ClientId client_id, std::string our_comp_id,
               std::string peer_comp_id)
        : engine_(engine),
          client_id_(client_id),
          encoder_(std::move(our_comp_id), std::move(peer_comp_id)) {}

    // Handle one inbound raw FIX message end to end: validate envelope,
    // parse, submit/cancel through the engine, and return the encoded
    // 35=8 ExecutionReport bytes. On a parse/envelope error, returns the
    // FixError (the caller decides whether to emit a FIX Reject / session
    // reject; encoding a business ExecutionReport for an unparseable
    // message isn't meaningful).
    std::variant<std::string, FixError> handle_message(std::string_view raw);

private:
    EngineAPI& engine_;
    ClientId client_id_;
    FixEncoder encoder_;
};

}  // namespace miniexchange::fix

#endif  // MINIEXCHANGE_ADAPTERS_FIX_FIX_SESSION_HPP
