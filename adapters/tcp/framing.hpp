#ifndef MINIEXCHANGE_ADAPTERS_TCP_FRAMING_HPP
#define MINIEXCHANGE_ADAPTERS_TCP_FRAMING_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace miniexchange::tcp {

// frame_message — wraps a payload in the TCP gateway's length-prefix
// framing format: [4-byte big-endian length][payload].
//
// Used by both:
// - The frame handler lambda (for sending parse-error responses
//   directly back to the client without an engine round-trip), and
// - The outbound response routing path (Task 5.5) for sending
//   serialized EngineResponses back to clients.
//
// The TcpServer's send_to_client() sends raw bytes; callers must
// frame outbound messages through this function so the client's read
// side (which expects length-prefixed frames) can reassemble them.
//
// Precondition: payload.size() <= kMaxFrameSize (4096). Callers are
// responsible for ensuring this — engine responses and error strings
// are well within this limit. No runtime check here (hot path).
[[nodiscard]] inline std::string frame_message(std::string_view payload) {
    auto len = static_cast<uint32_t>(payload.size());
    std::string frame(4 + len, '\0');
    frame[0] = static_cast<char>((len >> 24) & 0xFF);
    frame[1] = static_cast<char>((len >> 16) & 0xFF);
    frame[2] = static_cast<char>((len >> 8) & 0xFF);
    frame[3] = static_cast<char>(len & 0xFF);
    std::memcpy(frame.data() + 4, payload.data(), len);
    return frame;
}

}  // namespace miniexchange::tcp

#endif  // MINIEXCHANGE_ADAPTERS_TCP_FRAMING_HPP
