// Phase 9 — FIX adapter tests: tokenizer, envelope validation, parser
// (one case per error reason + happy paths), encoder (per OrdStatus +
// independent checksum), round-trip, and the full session integration.

#include <gtest/gtest.h>

#include <string>
#include <variant>

#include "adapters/fix/FixEncoder.hpp"
#include "adapters/fix/FixError.hpp"
#include "adapters/fix/FixMessage.hpp"
#include "adapters/fix/FixParser.hpp"
#include "adapters/fix/FixSession.hpp"
#include "core/EngineCommand.hpp"
#include "core/NewOrder.hpp"
#include "engine/matching_engine.hpp"

namespace miniexchange::fix {
namespace {

// Build a raw FIX message from field pairs, computing a correct
// BodyLength and CheckSum. This is a TEST helper — deliberately NOT the
// production encoder — so parser tests exercise independently-constructed
// bytes. `body_fields` are everything from tag 35 onward (the encoder's
// "body").
std::string build_message(const std::vector<std::pair<int, std::string>>& body_fields) {
    std::string body;
    for (auto& [tag, val] : body_fields) {
        body += std::to_string(tag) + "=" + val + kSOH;
    }
    std::string header;
    header += std::string("8=") + std::string(kBeginString) + kSOH;
    header += "9=" + std::to_string(body.size()) + kSOH;
    std::string without_cksum = header + body;

    unsigned sum = 0;
    for (unsigned char c : without_cksum) sum += c;
    char cbuf[8];
    std::snprintf(cbuf, sizeof(cbuf), "%03u", sum % 256);
    return without_cksum + "10=" + std::string(cbuf, 3) + kSOH;
}

std::string soh_to_pipe(std::string s) {
    for (auto& c : s) if (c == kSOH) c = '|';
    return s;
}

// ---------------------------------------------------------------------------
// Tokenizer (T3)
// ---------------------------------------------------------------------------

TEST(FixMessageTest, TokenizesValidMessage) {
    std::string raw = build_message({{35, "D"}, {55, "MINI"}, {54, "1"},
                                     {38, "100"}, {40, "2"}, {44, "10000"},
                                     {11, "42"}});
    auto r = FixMessage::tokenize(raw);
    ASSERT_TRUE(std::holds_alternative<TagMap>(r));
    const TagMap& m = std::get<TagMap>(r);
    EXPECT_EQ(m.get(35), "D");
    EXPECT_EQ(m.get(44), "10000");
    EXPECT_FALSE(m.has(99));
}

TEST(FixMessageTest, EmptyMessageRejected) {
    auto r = FixMessage::tokenize("");
    ASSERT_TRUE(std::holds_alternative<FixError>(r));
    EXPECT_EQ(std::get<FixError>(r).reason, FixErrorReason::EmptyMessage);
}

TEST(FixMessageTest, MissingFinalSohRejected) {
    // Field with no terminating SOH.
    std::string raw = std::string("8=FIX.4.2") + kSOH + "35=D";  // no final SOH
    auto r = FixMessage::tokenize(raw);
    ASSERT_TRUE(std::holds_alternative<FixError>(r));
    EXPECT_EQ(std::get<FixError>(r).reason, FixErrorReason::MalformedField);
}

TEST(FixMessageTest, FieldWithoutEqualsRejected) {
    std::string raw = std::string("8=FIX.4.2") + kSOH + "brokenfield" + kSOH;
    auto r = FixMessage::tokenize(raw);
    ASSERT_TRUE(std::holds_alternative<FixError>(r));
    EXPECT_EQ(std::get<FixError>(r).reason, FixErrorReason::MalformedField);
}

// ---------------------------------------------------------------------------
// Envelope validation: checksum + bodylength (T4)
// ---------------------------------------------------------------------------

TEST(FixEnvelopeTest, ValidChecksumAndBodyLengthPass) {
    std::string raw = build_message({{35, "D"}, {55, "MINI"}, {54, "1"},
                                     {38, "100"}, {40, "2"}, {44, "10000"},
                                     {11, "42"}});
    auto r = FixMessage::parse_validated(raw);
    ASSERT_TRUE(std::holds_alternative<TagMap>(r)) << soh_to_pipe(raw);
}

TEST(FixEnvelopeTest, CorruptedChecksumRejected) {
    std::string raw = build_message({{35, "D"}, {55, "MINI"}, {54, "1"},
                                     {38, "100"}, {40, "2"}, {44, "10000"},
                                     {11, "42"}});
    // Corrupt the checksum digits (last "NNN" before final SOH).
    raw[raw.size() - 4] = (raw[raw.size() - 4] == '0') ? '1' : '0';
    auto r = FixMessage::parse_validated(raw);
    ASSERT_TRUE(std::holds_alternative<FixError>(r));
    EXPECT_EQ(std::get<FixError>(r).reason, FixErrorReason::ChecksumMismatch);
}

TEST(FixEnvelopeTest, CorruptedBodyLengthRejected) {
    // Hand-build with a wrong BodyLength but a correct checksum, so the
    // bodylength check is what fires (checksum is validated first, so we
    // must keep it consistent).
    std::string body;
    for (auto& [tag, val] : std::vector<std::pair<int, std::string>>{
             {35, "D"}, {55, "MINI"}, {54, "1"}, {38, "100"}, {40, "2"},
             {44, "10000"}, {11, "42"}}) {
        body += std::to_string(tag) + "=" + val + kSOH;
    }
    std::string header = std::string("8=") + std::string(kBeginString) + kSOH;
    header += "9=" + std::to_string(body.size() + 5) + kSOH;  // WRONG length
    std::string without = header + body;
    unsigned sum = 0;
    for (unsigned char c : without) sum += c;
    char cbuf[8];
    std::snprintf(cbuf, sizeof(cbuf), "%03u", sum % 256);
    std::string raw = without + "10=" + std::string(cbuf, 3) + kSOH;

    auto r = FixMessage::parse_validated(raw);
    ASSERT_TRUE(std::holds_alternative<FixError>(r));
    EXPECT_EQ(std::get<FixError>(r).reason, FixErrorReason::BodyLengthMismatch);
}

// ---------------------------------------------------------------------------
// Parser: 35=D happy paths (T5)
// ---------------------------------------------------------------------------

ParsedMessage parse_ok(const std::string& raw, ClientId owner = ClientId{7}) {
    auto v = FixMessage::parse_validated(raw);
    EXPECT_TRUE(std::holds_alternative<TagMap>(v)) << soh_to_pipe(raw);
    auto p = FixParser::parse(std::get<TagMap>(v), owner);
    EXPECT_TRUE(std::holds_alternative<ParsedMessage>(p));
    return std::get<ParsedMessage>(p);
}

FixError parse_err(const std::string& raw) {
    auto v = FixMessage::parse_validated(raw);
    EXPECT_TRUE(std::holds_alternative<TagMap>(v)) << soh_to_pipe(raw);
    auto p = FixParser::parse(std::get<TagMap>(v), ClientId{7});
    EXPECT_TRUE(std::holds_alternative<FixError>(p));
    return std::get<FixError>(p);
}

TEST(FixParserTest, ValidLimitOrder) {
    auto msg = parse_ok(build_message({{35, "D"}, {11, "42"}, {55, "MINI"},
                                       {54, "1"}, {40, "2"}, {44, "10000"},
                                       {38, "50"}}),
                        ClientId{99});
    ASSERT_TRUE(std::holds_alternative<NewOrder>(msg));
    const NewOrder& no = std::get<NewOrder>(msg);
    ASSERT_TRUE(std::holds_alternative<LimitOrder>(no));
    const LimitOrder& lo = std::get<LimitOrder>(no);
    EXPECT_EQ(lo.id, OrderId{42});
    EXPECT_EQ(lo.side, Side::Buy);
    EXPECT_EQ(lo.price, Price{10000});
    EXPECT_EQ(lo.quantity, Quantity{50});
    EXPECT_EQ(lo.owner, ClientId{99});  // owner stamped from session
}

TEST(FixParserTest, ValidMarketOrder) {
    auto msg = parse_ok(build_message({{35, "D"}, {11, "7"}, {55, "MINI"},
                                       {54, "2"}, {40, "1"}, {38, "30"}}));
    ASSERT_TRUE(std::holds_alternative<NewOrder>(msg));
    const NewOrder& no = std::get<NewOrder>(msg);
    ASSERT_TRUE(std::holds_alternative<MarketOrder>(no));
    const MarketOrder& mo = std::get<MarketOrder>(no);
    EXPECT_EQ(mo.id, OrderId{7});
    EXPECT_EQ(mo.side, Side::Sell);
    EXPECT_EQ(mo.quantity, Quantity{30});
}

// ---------------------------------------------------------------------------
// Parser: 35=D one test per error reason (T5)
// ---------------------------------------------------------------------------

TEST(FixParserTest, NonNumericClOrdIdRejected) {
    EXPECT_EQ(parse_err(build_message({{35, "D"}, {11, "ABC"}, {55, "MINI"},
                                       {54, "1"}, {40, "2"}, {44, "100"},
                                       {38, "10"}}))
                  .reason,
              FixErrorReason::InvalidClOrdIdFormat);
}

TEST(FixParserTest, UnsupportedSymbolRejected) {
    EXPECT_EQ(parse_err(build_message({{35, "D"}, {11, "1"}, {55, "AAPL"},
                                       {54, "1"}, {40, "2"}, {44, "100"},
                                       {38, "10"}}))
                  .reason,
              FixErrorReason::UnsupportedSymbol);
}

TEST(FixParserTest, InvalidSideRejected) {
    EXPECT_EQ(parse_err(build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                       {54, "9"}, {40, "2"}, {44, "100"},
                                       {38, "10"}}))
                  .reason,
              FixErrorReason::InvalidSide);
}

TEST(FixParserTest, UnsupportedOrdTypeRejected) {
    EXPECT_EQ(parse_err(build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                       {54, "1"}, {40, "7"}, {38, "10"}}))
                  .reason,
              FixErrorReason::UnsupportedOrdType);
}

TEST(FixParserTest, MissingPriceOnLimitRejected) {
    EXPECT_EQ(parse_err(build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                       {54, "1"}, {40, "2"}, {38, "10"}}))
                  .reason,
              FixErrorReason::MissingPrice);
}

TEST(FixParserTest, InvalidQuantityRejected) {
    // Zero quantity.
    EXPECT_EQ(parse_err(build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                       {54, "1"}, {40, "2"}, {44, "100"},
                                       {38, "0"}}))
                  .reason,
              FixErrorReason::InvalidQuantity);
}

// ---------------------------------------------------------------------------
// Parser: 35=F cancel (T6) and reject-other-types (T7)
// ---------------------------------------------------------------------------

TEST(FixParserTest, CancelUsesOrigClOrdIdWhenPresent) {
    auto msg = parse_ok(build_message({{35, "F"}, {41, "123"}, {11, "999"}}));
    ASSERT_TRUE(std::holds_alternative<CancelRequest>(msg));
    // Tag 41 preferred over tag 11.
    EXPECT_EQ(std::get<CancelRequest>(msg).id, OrderId{123});
}

TEST(FixParserTest, CancelFallsBackToClOrdId) {
    auto msg = parse_ok(build_message({{35, "F"}, {11, "555"}}));
    ASSERT_TRUE(std::holds_alternative<CancelRequest>(msg));
    EXPECT_EQ(std::get<CancelRequest>(msg).id, OrderId{555});
}

TEST(FixParserTest, CancelMissingBothRejected) {
    EXPECT_EQ(parse_err(build_message({{35, "F"}, {58, "note"}})).reason,
              FixErrorReason::MissingRequiredTag);
}

TEST(FixParserTest, UnsupportedMessageTypeRejected) {
    // 35=A (Logon) is out of scope.
    EXPECT_EQ(parse_err(build_message({{35, "A"}, {98, "0"}, {108, "30"}}))
                  .reason,
              FixErrorReason::UnsupportedMessageType);
}

// ---------------------------------------------------------------------------
// Encoder: independent checksum + per-status OrdStatus (T10, T11)
// ---------------------------------------------------------------------------

// Independently recompute the checksum of an encoded message and confirm
// it matches the trailing 10=NNN. This catches a checksum bug rather than
// confirming self-consistency (design.md §7).
void assert_checksum_correct(const std::string& msg) {
    std::string needle = std::string(1, kSOH) + "10=";
    auto pos = msg.rfind(needle);
    ASSERT_NE(pos, std::string::npos);
    std::string before = msg.substr(0, pos + 1);  // include SOH before 10=
    unsigned sum = 0;
    for (unsigned char c : before) sum += c;
    unsigned expected = sum % 256;
    // Extract the declared checksum digits.
    std::string decl = msg.substr(pos + needle.size(), 3);
    EXPECT_EQ(std::stoi(decl), static_cast<int>(expected))
        << "message: " << soh_to_pipe(msg);
}

EngineResponse make_response(EngineResult status,
                             std::vector<Trade> trades = {},
                             Quantity remaining = Quantity{0}) {
    return EngineResponse{status, std::move(trades), remaining};
}

TEST(FixEncoderTest, NewOrderStatus) {
    FixEncoder enc("EX", "CLIENT");
    ExecReportContext ctx{OrderId{1}, Side::Buy, Quantity{100}, false};
    auto msg = enc.encode_execution_report(
        ctx, make_response(EngineResult::Accepted, {}, Quantity{100}));
    assert_checksum_correct(msg);
    // OrdStatus (39) should be '0' (New).
    EXPECT_NE(msg.find(std::string(1, kSOH) + "39=0" + kSOH), std::string::npos)
        << soh_to_pipe(msg);
}

TEST(FixEncoderTest, PartiallyFilledStatus) {
    FixEncoder enc("EX", "CLIENT");
    ExecReportContext ctx{OrderId{2}, Side::Buy, Quantity{100}, false};
    std::vector<Trade> trades = {
        {TradeSequence{1}, OrderId{2}, OrderId{9}, Price{100}, Quantity{40}, true}};
    auto msg = enc.encode_execution_report(
        ctx, make_response(EngineResult::Accepted, trades, Quantity{60}));
    assert_checksum_correct(msg);
    EXPECT_NE(msg.find(std::string(1, kSOH) + "39=1" + kSOH), std::string::npos);
}

TEST(FixEncoderTest, FilledStatus) {
    FixEncoder enc("EX", "CLIENT");
    ExecReportContext ctx{OrderId{3}, Side::Sell, Quantity{50}, false};
    std::vector<Trade> trades = {
        {TradeSequence{1}, OrderId{8}, OrderId{3}, Price{100}, Quantity{50}, true}};
    auto msg = enc.encode_execution_report(
        ctx, make_response(EngineResult::Accepted, trades, Quantity{0}));
    assert_checksum_correct(msg);
    EXPECT_NE(msg.find(std::string(1, kSOH) + "39=2" + kSOH), std::string::npos);
}

TEST(FixEncoderTest, CancelledStatus) {
    FixEncoder enc("EX", "CLIENT");
    ExecReportContext ctx{OrderId{4}, Side::Buy, Quantity{0}, /*is_cancel=*/true};
    auto msg = enc.encode_execution_report(
        ctx, make_response(EngineResult::Accepted));
    assert_checksum_correct(msg);
    EXPECT_NE(msg.find(std::string(1, kSOH) + "39=4" + kSOH), std::string::npos);
}

TEST(FixEncoderTest, RejectedStatus) {
    FixEncoder enc("EX", "CLIENT");
    ExecReportContext ctx{OrderId{5}, Side::Buy, Quantity{100}, false};
    auto msg = enc.encode_execution_report(
        ctx, make_response(EngineResult::QuantityTooLarge));
    assert_checksum_correct(msg);
    EXPECT_NE(msg.find(std::string(1, kSOH) + "39=8" + kSOH), std::string::npos);
}

TEST(FixEncoderTest, BodyLengthIsCorrect) {
    FixEncoder enc("SENDER", "TARGET");
    ExecReportContext ctx{OrderId{1}, Side::Buy, Quantity{10}, false};
    auto msg = enc.encode_execution_report(
        ctx, make_response(EngineResult::Accepted, {}, Quantity{10}));
    // The message must itself pass envelope validation (bodylength +
    // checksum) — round-trip through the validator.
    auto v = FixMessage::parse_validated(msg);
    EXPECT_TRUE(std::holds_alternative<TagMap>(v)) << soh_to_pipe(msg);
}

// ---------------------------------------------------------------------------
// Round-trip (T12): NewOrder -> 35=D bytes -> parse -> matches
// ---------------------------------------------------------------------------

TEST(FixRoundTripTest, LimitOrderRoundTrips) {
    std::string raw = build_message({{35, "D"}, {11, "1001"}, {55, "MINI"},
                                     {54, "1"}, {40, "2"}, {44, "12345"},
                                     {38, "77"}});
    auto msg = parse_ok(raw, ClientId{3});
    const LimitOrder& lo = std::get<LimitOrder>(std::get<NewOrder>(msg));
    EXPECT_EQ(lo.id, OrderId{1001});
    EXPECT_EQ(lo.price, Price{12345});
    EXPECT_EQ(lo.quantity, Quantity{77});
    EXPECT_EQ(lo.side, Side::Buy);
}

TEST(FixRoundTripTest, MarketOrderRoundTrips) {
    std::string raw = build_message({{35, "D"}, {11, "2002"}, {55, "MINI"},
                                     {54, "2"}, {40, "1"}, {38, "5"}});
    auto msg = parse_ok(raw);
    const MarketOrder& mo = std::get<MarketOrder>(std::get<NewOrder>(msg));
    EXPECT_EQ(mo.id, OrderId{2002});
    EXPECT_EQ(mo.quantity, Quantity{5});
    EXPECT_EQ(mo.side, Side::Sell);
}

// ---------------------------------------------------------------------------
// Integration (T13, T14): full session parse -> submit -> encode
// ---------------------------------------------------------------------------

TEST(FixSessionTest, LimitOrderSubmittedAndReported) {
    MatchingEngine engine;
    FixSession session(engine, ClientId{5}, "EX", "CLIENT");

    std::string raw = build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                     {54, "1"}, {40, "2"}, {44, "10000"},
                                     {38, "50"}});
    auto r = session.handle_message(raw);
    ASSERT_TRUE(std::holds_alternative<std::string>(r));
    // The order rested (no cross), so status should be New (39=0).
    const std::string& report = std::get<std::string>(r);
    EXPECT_NE(report.find(std::string(1, kSOH) + "39=0" + kSOH),
              std::string::npos)
        << soh_to_pipe(report);
    // And it's in the book with the session's owner.
    Order* resting = engine.book().find_order(OrderId{1});
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->owner, ClientId{5});
}

TEST(FixSessionTest, CrossingOrdersProduceFilledReport) {
    MatchingEngine engine;
    FixSession session(engine, ClientId{5}, "EX", "CLIENT");

    // Rest a sell, then a crossing buy that fully fills.
    session.handle_message(build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                          {54, "2"}, {40, "2"}, {44, "10000"},
                                          {38, "50"}}));
    auto r = session.handle_message(build_message({{35, "D"}, {11, "2"},
                                                   {55, "MINI"}, {54, "1"},
                                                   {40, "2"}, {44, "10000"},
                                                   {38, "50"}}));
    ASSERT_TRUE(std::holds_alternative<std::string>(r));
    const std::string& report = std::get<std::string>(r);
    // Aggressor fully filled -> 39=2.
    EXPECT_NE(report.find(std::string(1, kSOH) + "39=2" + kSOH),
              std::string::npos)
        << soh_to_pipe(report);
}

TEST(FixSessionTest, CancelReportsCancelled) {
    MatchingEngine engine;
    FixSession session(engine, ClientId{5}, "EX", "CLIENT");

    session.handle_message(build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                          {54, "1"}, {40, "2"}, {44, "10000"},
                                          {38, "50"}}));
    auto r = session.handle_message(build_message({{35, "F"}, {41, "1"}}));
    ASSERT_TRUE(std::holds_alternative<std::string>(r));
    EXPECT_NE(std::get<std::string>(r).find(std::string(1, kSOH) + "39=4" + kSOH),
              std::string::npos);
    EXPECT_EQ(engine.book().find_order(OrderId{1}), nullptr);
}

TEST(FixSessionTest, MalformedMessageReturnsError) {
    MatchingEngine engine;
    FixSession session(engine, ClientId{5}, "EX", "CLIENT");
    // Corrupt checksum.
    std::string raw = build_message({{35, "D"}, {11, "1"}, {55, "MINI"},
                                     {54, "1"}, {40, "2"}, {44, "10000"},
                                     {38, "50"}});
    raw[raw.size() - 4] = (raw[raw.size() - 4] == '9') ? '0' : '9';
    auto r = session.handle_message(raw);
    ASSERT_TRUE(std::holds_alternative<FixError>(r));
    EXPECT_EQ(std::get<FixError>(r).reason, FixErrorReason::ChecksumMismatch);
}

}  // namespace
}  // namespace miniexchange::fix
