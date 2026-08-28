#ifndef MINIEXCHANGE_CORE_TAGGED_COMMAND_HPP
#define MINIEXCHANGE_CORE_TAGGED_COMMAND_HPP

#include "EngineCommand.hpp"
#include "Events.hpp"
#include "Types.hpp"

namespace miniexchange {

// TaggedCommand — the inbound SPSC queue payload: an engine command
// tagged with the ClientId of the connection that submitted it. The
// engine thread processes the command and tags its response with the
// same ClientId for routing back to the correct client.
//
// Default-constructible: required by SpscRingBuffer<T, N>'s internal
// std::array<T, N> storage. EngineCommand (a std::variant) default-
// constructs to its first alternative (LimitOrder), and ClientId has
// a default constructor — so TaggedCommand is trivially default-
// constructible without any extra work.
struct TaggedCommand {
    ClientId client;
    EngineCommand command;
};

// TaggedResponse — the outbound SPSC queue payload: an engine response
// tagged with the ClientId it should be routed to. The I/O thread looks
// up the connection by ClientId and writes the serialized response to
// that specific client's write buffer.
//
// Holds EngineResponse by value. The vector<Trade> inside transfers
// ownership via move when pushed/popped through the SPSC queue —
// no extra allocation at routing time.
//
// Default-constructible: EngineResult (enum) zero-initializes,
// std::vector<Trade> default-constructs to empty, Quantity has a
// default constructor, and ClientId has a default constructor.
struct TaggedResponse {
    ClientId client;
    EngineResponse response;
};

}  // namespace miniexchange

#endif  // MINIEXCHANGE_CORE_TAGGED_COMMAND_HPP
