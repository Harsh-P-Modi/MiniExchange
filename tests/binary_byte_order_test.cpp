// tests/binary_byte_order_test.cpp — Unit tests for byte-order helpers.
// Verifies round-trip correctness for all integer widths and core/
// strong-typed wrappers.

#include "adapters/binary_protocol/ByteOrder.hpp"
#include "core/Types.hpp"

#include <gtest/gtest.h>
#include <climits>

namespace miniexchange::binary_protocol {
namespace {

// --- 16-bit ---

TEST(ByteOrderTest, Uint16RoundTrip) {
    uint16_t val = 0x0102;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Uint16Zero) {
    uint16_t val = 0;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Uint16Max) {
    uint16_t val = UINT16_MAX;
    EXPECT_EQ(from_network(to_network(val)), val);
}

// --- 32-bit unsigned ---

TEST(ByteOrderTest, Uint32RoundTrip) {
    uint32_t val = 0x01020304;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Uint32Zero) {
    uint32_t val = 0;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Uint32Max) {
    uint32_t val = UINT32_MAX;
    EXPECT_EQ(from_network(to_network(val)), val);
}

// --- 32-bit signed ---

TEST(ByteOrderTest, Int32RoundTrip) {
    int32_t val = 0x01020304;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Int32Negative) {
    int32_t val = -42;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Int32Min) {
    int32_t val = INT32_MIN;
    EXPECT_EQ(from_network(to_network(val)), val);
}

// --- 64-bit unsigned ---

TEST(ByteOrderTest, Uint64RoundTrip) {
    uint64_t val = 0x0102030405060708ULL;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Uint64Zero) {
    uint64_t val = 0;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Uint64Max) {
    uint64_t val = UINT64_MAX;
    EXPECT_EQ(from_network(to_network(val)), val);
}

// --- 64-bit signed (Price's underlying type) ---

TEST(ByteOrderTest, Int64RoundTrip) {
    int64_t val = 0x0102030405060708LL;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Int64Negative) {
    int64_t val = -42;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Int64Min) {
    int64_t val = INT64_MIN;
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, Int64Max) {
    int64_t val = INT64_MAX;
    EXPECT_EQ(from_network(to_network(val)), val);
}

// --- Core strong types ---

TEST(ByteOrderTest, OrderIdRoundTrip) {
    OrderId val{0x0102030405060708ULL};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, OrderIdMax) {
    OrderId val{UINT64_MAX};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, OrderIdZero) {
    OrderId val{0};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, ClientIdRoundTrip) {
    ClientId val{0xDEADBEEFCAFEBABEULL};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, PricePositive) {
    Price val{100};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, PriceNegative) {
    // Price is signed int64_t — verify sign bit is preserved across swap.
    Price val{-42};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, PriceMin) {
    Price val{INT64_MIN};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, PriceMax) {
    Price val{INT64_MAX};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, QuantityRoundTrip) {
    Quantity val{1000000};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, QuantityMax) {
    Quantity val{UINT64_MAX};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, QuantityZero) {
    Quantity val{0};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, SequenceRoundTrip) {
    Sequence val{999999};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, TradeSequenceRoundTrip) {
    TradeSequence val{0xABCDEF0123456789ULL};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, SymbolIdRoundTrip) {
    // SymbolId is uint32_t-backed — the only 32-bit strong type.
    SymbolId val{0x01020304};
    EXPECT_EQ(from_network(to_network(val)), val);
}

TEST(ByteOrderTest, SymbolIdMax) {
    SymbolId val{UINT32_MAX};
    EXPECT_EQ(from_network(to_network(val)), val);
}

// --- Verify swap actually changes the representation ---
// On little-endian hosts (x86, which is the project's target), to_network
// should NOT be a no-op for multi-byte values. This test catches an
// accidental identity implementation.

TEST(ByteOrderTest, SwapIsNotIdentityOnLittleEndian) {
    // This test is meaningful on little-endian only. On big-endian,
    // to_network IS the identity, and this test would need to be skipped.
    // Since the project targets x86 Linux, we assert non-identity.
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint64_t val = 0x0102030405060708ULL;
    uint64_t swapped = to_network(val);
    EXPECT_NE(val, swapped);
    // After swap, the first byte (most significant in network order)
    // should be 0x01 when read as a byte array.
    auto* bytes = reinterpret_cast<const uint8_t*>(&swapped);
    EXPECT_EQ(bytes[0], 0x01);
    EXPECT_EQ(bytes[7], 0x08);
#endif
}

}  // namespace
}  // namespace miniexchange::binary_protocol
