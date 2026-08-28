#ifndef MINIEXCHANGE_ADAPTERS_TEXT_PROTOCOL_RENDERER_HPP
#define MINIEXCHANGE_ADAPTERS_TEXT_PROTOCOL_RENDERER_HPP

#include <string>

#include "core/Events.hpp"

namespace miniexchange::text_protocol {

// Renders an EngineResponse into a human-readable text string suitable
// for sending over TCP (or printing to a console). This is the inverse
// of parse(): the TCP gateway serializes engine results via this function
// before writing them into a connection's send buffer.
//
// Output format:
//   Rejection:      "REJECTED: <reason>\n"
//   Accepted/empty: "ACCEPTED: no fills, remaining_qty=<qty>\n"
//   Accepted/fills: "ACCEPTED: FILL <qty>@<price> ... | remaining_qty=<qty>\n"
//                   or "... | FULLY FILLED\n" when remaining is 0.
[[nodiscard]] std::string render(const EngineResponse& response);

// Renders a protocol-level error message (e.g. parse failure) into an
// in-band text error response. Used when the I/O thread detects a
// malformed frame and responds directly without an engine round-trip.
//
// Output format: "ERROR: <message>\n"
[[nodiscard]] std::string render_error(const std::string& message);

}  // namespace miniexchange::text_protocol

#endif  // MINIEXCHANGE_ADAPTERS_TEXT_PROTOCOL_RENDERER_HPP
