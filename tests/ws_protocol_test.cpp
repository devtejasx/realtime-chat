#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rtc/realtime/connection_manager.hpp"
#include "rtc/realtime/protocol.hpp"

namespace {

using rtc::realtime::protocol::Envelope;
using rtc::realtime::protocol::Version;

// --- negotiation ----------------------------------------------------------

TEST(WsProtocol, DefaultsToVersionOneWhenNothingIsRequested) {
    // The compatibility contract: a client that knows nothing about versioning
    // must keep receiving exactly the format it always has.
    EXPECT_EQ(rtc::realtime::protocol::negotiate(std::nullopt), Version::kV1);
    EXPECT_EQ(rtc::realtime::protocol::kDefaultVersion, Version::kV1);
}

TEST(WsProtocol, NegotiatesSupportedVersions) {
    EXPECT_EQ(rtc::realtime::protocol::negotiate(1), Version::kV1);
    EXPECT_EQ(rtc::realtime::protocol::negotiate(2), Version::kV2);
}

TEST(WsProtocol, FallsBackRatherThanRejectingAnUnknownVersion) {
    // A client asking for a future version should get a working connection, not a
    // failed handshake.
    EXPECT_EQ(rtc::realtime::protocol::negotiate(99), Version::kV1);
    EXPECT_EQ(rtc::realtime::protocol::negotiate(0), Version::kV1);
    EXPECT_EQ(rtc::realtime::protocol::negotiate(-3), Version::kV1);
}

// --- v1 encoding (frozen format) ------------------------------------------

TEST(WsProtocol, V1EncodesTheHistoricalShapeExactly) {
    const auto encoded = nlohmann::json::parse(rtc::realtime::protocol::encode(
        Version::kV1, "message.created", nlohmann::json{{"id", 7}}));

    EXPECT_EQ(encoded.at("type"), "message.created");
    EXPECT_EQ(encoded.at("data").at("id"), 7);
    // v1 is frozen: adding keys here could break a strict legacy client, which is
    // the entire reason version negotiation exists.
    EXPECT_EQ(encoded.size(), 2U);
}

TEST(WsProtocol, V1IgnoresCorrelationMetadata) {
    const auto encoded = nlohmann::json::parse(rtc::realtime::protocol::encode(
        Version::kV1, "pong", nlohmann::json::object(), Envelope{"req-1", "corr-1"}));
    EXPECT_EQ(encoded.size(), 2U);
    EXPECT_FALSE(encoded.contains("request_id"));
}

TEST(WsProtocol, ConnectionManagerEnvelopeStillEmitsV1) {
    // make_envelope is part of the tested surface and documented as the legacy
    // format; it must not drift onto v2.
    const auto encoded = nlohmann::json::parse(
        rtc::realtime::ConnectionManager::make_envelope("typing.start",
                                                        nlohmann::json{{"user_id", 3}}));
    EXPECT_EQ(encoded.at("type"), "typing.start");
    EXPECT_EQ(encoded.at("data").at("user_id"), 3);
    EXPECT_EQ(encoded.size(), 2U);
}

// --- v2 encoding ----------------------------------------------------------

TEST(WsProtocol, V2CarriesTheFullEnvelope) {
    const auto encoded = nlohmann::json::parse(rtc::realtime::protocol::encode(
        Version::kV2, "message.created", nlohmann::json{{"id", 7}},
        Envelope{"req-42", "corr-99"}));

    EXPECT_EQ(encoded.at("event"), "message.created");
    EXPECT_EQ(encoded.at("version"), 2);
    EXPECT_EQ(encoded.at("payload").at("id"), 7);
    EXPECT_EQ(encoded.at("request_id"), "req-42");
    EXPECT_EQ(encoded.at("correlation_id"), "corr-99");
    // ISO-8601 UTC, e.g. 2026-07-30T12:00:00Z
    const std::string timestamp = encoded.at("timestamp");
    EXPECT_EQ(timestamp.size(), 20U);
    EXPECT_EQ(timestamp.back(), 'Z');
}

TEST(WsProtocol, V2AlwaysIncludesCorrelationKeysEvenWhenUnknown) {
    // Present-but-null rather than absent, so client code can read them
    // unconditionally.
    const auto encoded = nlohmann::json::parse(rtc::realtime::protocol::encode(
        Version::kV2, "presence.update", nlohmann::json{{"status", "online"}}));
    ASSERT_TRUE(encoded.contains("request_id"));
    ASSERT_TRUE(encoded.contains("correlation_id"));
    EXPECT_TRUE(encoded.at("request_id").is_null());
    EXPECT_TRUE(encoded.at("correlation_id").is_null());
}

TEST(WsProtocol, V2ContainsEveryFieldTheProtocolPromises) {
    const auto encoded = nlohmann::json::parse(
        rtc::realtime::protocol::encode(Version::kV2, "ready", nlohmann::json::object()));
    for (const char* key :
         {"event", "version", "timestamp", "request_id", "correlation_id", "payload"}) {
        EXPECT_TRUE(encoded.contains(key)) << "missing " << key;
    }
}

// --- decoding -------------------------------------------------------------

TEST(WsProtocol, DecodesTheV1ClientShape) {
    const auto frame = rtc::realtime::protocol::decode(
        R"({"type":"message.send","data":{"conversation_id":5,"content":"hi"}})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->version, Version::kV1);
    EXPECT_EQ(frame->event, "message.send");
    EXPECT_EQ(frame->payload.at("conversation_id"), 5);
    EXPECT_TRUE(frame->request_id.empty());
}

TEST(WsProtocol, DecodesTheV2ClientShape) {
    const auto frame = rtc::realtime::protocol::decode(
        R"({"event":"message.send","payload":{"conversation_id":5},"request_id":"r1",)"
        R"("correlation_id":"c1"})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->version, Version::kV2);
    EXPECT_EQ(frame->event, "message.send");
    EXPECT_EQ(frame->payload.at("conversation_id"), 5);
    EXPECT_EQ(frame->request_id, "r1");
    EXPECT_EQ(frame->correlation_id, "c1");
}

TEST(WsProtocol, PrefersTheV2ShapeWhenBothKeysArePresent) {
    const auto frame = rtc::realtime::protocol::decode(
        R"({"event":"a","payload":{"x":1},"type":"b","data":{"y":2}})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->event, "a");
    EXPECT_EQ(frame->payload.at("x"), 1);
}

TEST(WsProtocol, TreatsAMissingPayloadAsEmpty) {
    const auto frame = rtc::realtime::protocol::decode(R"({"event":"ping"})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->payload.is_object());
    EXPECT_TRUE(frame->payload.empty());
}

TEST(WsProtocol, RejectsUndispatchableFrames) {
    // A malformed frame is ordinary client input, so decoding must fail cleanly
    // rather than throw — the dispatcher turns nullopt into an error event.
    const char* bad[] = {
        "",                        // empty
        "not json",                // unparseable
        "[]",                      // not an object
        "42",                      // not an object
        R"({"data":{"a":1}})",     // no event name
        R"({"type":"","data":{}})", // empty event name
        R"({"type":123})",         // non-string event name
    };
    for (const char* text : bad) {
        EXPECT_FALSE(rtc::realtime::protocol::decode(text).has_value()) << text;
    }
}

TEST(WsProtocol, IgnoresANonObjectPayload) {
    // Defensive: the handlers index into the payload as an object.
    const auto frame = rtc::realtime::protocol::decode(R"({"event":"ping","payload":"nope"})");
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->payload.is_object());
    EXPECT_TRUE(frame->payload.empty());
}

TEST(WsProtocol, RoundTripsThroughEncodeAndDecode) {
    const nlohmann::json payload{{"conversation_id", 9}, {"content", "round trip"}};
    for (const Version version : {Version::kV1, Version::kV2}) {
        const auto frame = rtc::realtime::protocol::decode(
            rtc::realtime::protocol::encode(version, "message.send", payload,
                                            Envelope{"req", "corr"}));
        ASSERT_TRUE(frame.has_value());
        EXPECT_EQ(frame->event, "message.send");
        EXPECT_EQ(frame->payload, payload);
    }
}

}  // namespace
