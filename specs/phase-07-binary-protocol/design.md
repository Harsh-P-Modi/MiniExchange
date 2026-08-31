# Phase 7 — Design: Binary Wire Protocol

Status: **APPROVED** — `tasks.md` is written from this version and the
phase is implemented and test-verified.

## 0. Resolved Open Questions (from requirements.md)

| # | Question | Resolution |
|---|----------|------------|
| 1 | JSON library | `nlohmann/json`, `FetchContent`-vendored. Benchmark-only; never a production format. |
| 2 | Endianness | Network byte order, implemented correctly via byte-swap helpers (§2), not left as a documented limitation. |
| 3 | Plaintext fallback | Kept as a server-startup-selected debug mode (§5), not a per-connection-negotiated one. |

## 1. Architecture Overview

```
adapters/binary_protocol/
├── ByteOrder.hpp        — network byte order helpers, wraps core/ strong types
├── Message.hpp          — the six on-wire message structs + MessageType enum
├── BinaryCodec.hpp/.cpp — encode(Message) -> bytes, decode(bytes) -> Message
├── JsonCodec.hpp/.cpp   — same message types, nlohmann/json, benchmark-only
└── ProtocolHandler.hpp  — implements Phase 5's existing protocol-handler
                            interface, dispatching to BinaryCodec

tools/protocol_benchmark/ — reuses Phase 2's benchmarking harness format,
                             drives BinaryCodec and JsonCodec head-to-head
```

Phase 5's TCP gateway already factored request parsing behind a
protocol-handler seam (that's what let `PRINT_BOOK`/`QUIT` be split out
of the shared text parser during Phase 5's review). Phase 7 adds a
second implementation of that same seam — `BinaryProtocolHandler` —
rather than modifying the plaintext one. The gateway's connection
loop is unaware of which handler it's holding; it just calls
`parse_message(bytes) -> ClientRequest` and `serialize_response(...)
-> bytes` on whichever handler the server was started with (§5).

## 2. Wire Format and Byte Order

Six message types cover the scope in requirements.md R1: three
client→server requests, three server→client responses.

```cpp
// adapters/binary_protocol/Message.hpp

enum class MessageType : uint8_t {
    LimitOrderAdd = 1,
    MarketOrderAdd = 2,
    Cancel = 3,
    Ack = 4,
    Reject = 5,
    TradeNotification = 6,
};

struct LimitOrderAddMsg {
    MessageType type = MessageType::LimitOrderAdd;
    Side       side;
    ClientId   client_id;
    OrderId    order_id;      // client-assigned correlation id
    Price      price;
    Quantity   quantity;
};

struct MarketOrderAddMsg {
    MessageType type = MessageType::MarketOrderAdd;
    Side       side;
    ClientId   client_id;
    OrderId    order_id;
    Quantity   quantity;
};

struct CancelMsg {
    MessageType type = MessageType::Cancel;
    ClientId   client_id;
    OrderId    order_id;
};

struct AckMsg {
    MessageType type = MessageType::Ack;
    OrderId    order_id;
};

struct RejectMsg {
    MessageType type = MessageType::Reject;
    OrderId    order_id;
    uint8_t    reason_code;
};

struct TradeNotificationMsg {
    MessageType   type = MessageType::TradeNotification;
    OrderId       buy_order_id;
    OrderId       sell_order_id;
    Price         price;
    Quantity      quantity;
    TradeSequence trade_sequence;
};
```

All six reuse `core/` strong types (`Side`, `ClientId`, `OrderId`,
`Price`, `Quantity`, `TradeSequence`) exactly as Phase 5/6 do — no
redefinition.

### Byte order

Every multi-byte field is transmitted in network byte order (big-endian). Since the domain's strong types wrap integers wider than the 16/32-bit `htons`/`htonl` cover in the common case (`Price`/`Quantity`/`OrderId`/etc. are 64-bit-backed per earlier phases), the byte-order helpers operate generically on the wrapper's underlying value:

```cpp
// adapters/binary_protocol/ByteOrder.hpp

// Raw integer swaps — 16/32-bit via <arpa/inet.h>, 64-bit via <endian.h>'s
// htobe64/be64toh (Linux; this project's target platform per network config).
inline uint16_t to_network(uint16_t v) { return htons(v); }
inline uint32_t to_network(uint32_t v) { return htonl(v); }
inline uint64_t to_network(uint64_t v) { return htobe64(v); }
inline uint16_t from_network(uint16_t v) { return ntohs(v); }
inline uint32_t from_network(uint32_t v) { return ntohl(v); }
inline uint64_t from_network(uint64_t v) { return be64toh(v); }

// Generic overload for core/'s strong-typed wrappers (OrderId, Price,
// Quantity, ClientId, TradeSequence, ...) — each is swapped by
// extracting, swapping, and rewrapping its underlying value. Single-byte
// types (Side, MessageType, reason_code) need no swapping at all and
// are excluded from this by their size.
template <typename T>
    requires (sizeof(typename T::underlying_type) > 1)
T to_network(T wrapped) {
    return T{to_network(wrapped.value)};
}
```

`BinaryCodec::encode` calls `to_network` on every multi-byte field
before writing it into the output buffer; `decode` calls
`from_network` on every field immediately after reading it. This is
applied uniformly and mechanically per field — there is no per-message
special-casing of which fields need swapping, which keeps new message
types trivial to add correctly.

## 3. Encode/Decode API — No Heap Allocation (NFR1)

```cpp
// adapters/binary_protocol/BinaryCodec.hpp

using AnyMessage = std::variant<LimitOrderAddMsg, MarketOrderAddMsg,
                                 CancelMsg, AckMsg, RejectMsg,
                                 TradeNotificationMsg>;

// Writes into a caller-provided buffer; returns bytes written.
// No allocation: buffer is caller-owned (stack array or a pre-sized
// connection write-buffer), and every field write is a fixed-size
// byte-order-swapped memcpy.
template <typename Msg>
size_t encode(const Msg& msg, std::span<std::byte> out);

// Reads the leading MessageType byte to determine which fixed-size
// struct follows, then decodes into a stack-constructed AnyMessage.
// Returns std::nullopt if `in` is shorter than the expected size for
// the type byte read, or the type byte is unrecognized.
std::optional<AnyMessage> decode(std::span<const std::byte> in);
```

No message type has a variable-length field — every size is known at
compile time from the struct's layout, so `decode` never needs to
allocate to hold intermediate data; it reads directly into a
stack-allocated `AnyMessage`. `std::variant` itself does not
heap-allocate for value types (its storage is inline), which is why it
was chosen over e.g. a `unique_ptr<Message>`-based hierarchy.

Verifying NFR1 isn't just an architectural claim — task 8 instruments
a test-scoped global `operator new`/`operator delete` counter around
calls to `encode`/`decode` and asserts the count is unchanged,
following the same instrumented-allocation-counting approach used to
verify Phase 3's memory pool.

## 4. JSON Codec (Benchmark-Only)

```cpp
// adapters/binary_protocol/JsonCodec.hpp — nlohmann/json, ADL to_json/from_json
// for each of the six message structs. Field names are the same as the
// C++ struct field names, for readability in the benchmark's captured
// payloads, not for any wire-compatibility goal.

void to_json(nlohmann::json& j, const LimitOrderAddMsg& msg);
void from_json(const nlohmann::json& j, LimitOrderAddMsg& msg);
// ... one pair per message type
```

This codec is expected to allocate — that's not a bug to fix, it's the
comparison point. `nlohmann::json`'s internal representation, string
serialization, and parsing all involve heap activity by construction;
NFR1 applies only to the binary codec (requirements.md NFR1 says
"binary encode/decode," not "all encode/decode").

## 5. Server-Startup Protocol Selection (Not Per-Connection)

The gateway is started with a single mode for its whole lifetime —
`--protocol=binary` (default) or `--protocol=plaintext` (debug) — set
once at `apps/exchange_server/main.cpp` startup, which constructs
either a `BinaryProtocolHandler` or the existing plaintext handler and
hands it to the connection loop. Every client connection on that
server instance speaks the same protocol.

**Why not per-connection negotiation** (e.g., a preamble byte, or
protocol auto-detection by sniffing the first bytes)? Considered and
rejected: it would mean every connection pays a detection cost and the
gateway would need to keep both parsers warm simultaneously, for a
capability nothing in this project's scope actually needs — the
plaintext mode exists for manual `netcat` testing during development,
not for mixed production traffic. A startup flag gets the stated
benefit (plaintext still available for debugging) with a single-line
`main.cpp` branch instead of a protocol-sniffing state machine.

## 6. Benchmark (R4)

`tools/protocol_benchmark/` reuses Phase 2's `workload_generator`
harness and results format (same reasoning: comparable numbers across
phases matter more than a bespoke benchmark setup for this one). For
each of the six message types, it measures:

- **Encode latency**: time to encode a representative populated
  instance, binary vs. JSON, over N iterations (same N-and-statistics
  approach as Phase 2 — min/median/p99, not just mean).
- **Decode latency**: same, for decode.
- **Payload size**: `sizeof`/`encode()`'s returned byte count for
  binary; `nlohmann::json::dump().size()` for JSON — both measured,
  not assumed from message field counts, since JSON's size depends on
  field name lengths and number formatting.

The write-up (Definition of Done) is required to attribute *where*
JSON's disadvantage (if any) comes from — allocation count and
parse/format CPU time are reported as separate lines, not folded into
one latency number, specifically so the interpretation task isn't
guesswork.

## 7. Why Not X

- **Why not variable-length/TLV encoding instead of fixed structs?** Every message type in this phase's scope has a fixed, known shape — there's no field whose length varies per instance. TLV would add framing overhead and decode complexity to solve a problem (variable-length payloads) this protocol doesn't have. Revisit only if a future phase adds a genuinely variable-length message type.
- **Why not use `htons`/`htonl` uniformly and truncate/split 64-bit fields?** Would require splitting every wide field into two 32-bit halves at each call site, which is exactly the kind of per-field special-casing §2's generic `to_network` overload set out to avoid. `htobe64`/`be64toh` do the same job for 64-bit values directly and are available on the project's Linux target.
- **Why not skip endianness correctness (little-endian-only) given it's a single-machine dev/demo setup?** Considered per the requirements' framing, but rejected: it's cheap to do correctly here (a handful of swap-helper calls), and doing it properly is itself one of the things worth demonstrating for the portfolio's audience — a protocol that only works between machines sharing endianness isn't really a "wire protocol" in the sense the phase's scope claims.
- **Why not make JSON a supported production fallback instead of plaintext?** JSON was scoped as benchmark-only from requirements.md R3 — introducing it as a second production format would double the gateway's maintained protocol surface for no benefit plaintext doesn't already provide (human-readable manual testing).
- **Why a `std::variant`-based `AnyMessage` instead of a polymorphic message hierarchy?** A class hierarchy would need virtual dispatch and (absent a custom allocator) typically heap-allocated instances behind a `unique_ptr`, which conflicts directly with NFR1. `std::variant` stores any of its alternatives inline, so decode can construct one on the stack with no allocation.

## 8. Non-Goals

- No support for message types beyond the six listed in R1 — no order-modify, no multi-leg messages.
- No protocol versioning/negotiation scheme — this is a single fixed protocol version for this project's lifetime, not a forward-compatible wire format.
- No compression of any kind — out of scope and would obscure the raw binary-vs-JSON comparison this phase exists to produce.
- No per-connection protocol negotiation (§5).
