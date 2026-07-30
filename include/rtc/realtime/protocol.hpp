#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace rtc::realtime::protocol {

// Versioned WebSocket wire protocol.
//
// Version 1 (the format this service has always spoken):
//
//     {"type": "message.created", "data": { ... }}
//
// Version 2 adds the metadata an operator needs to correlate a frame with the
// request and trace that produced it:
//
//     {
//       "event":          "message.created",
//       "version":        2,
//       "timestamp":      "2026-07-30T12:00:00Z",
//       "request_id":     "9f2c1ab4",
//       "correlation_id": "4c1d...",
//       "payload":        { ... }
//     }
//
// Why negotiate rather than always emit v2
// ----------------------------------------
// The obvious alternative is to emit a union of both shapes — keep `type`/`data`
// and add the v2 keys alongside. That preserves compatibility too, but it means
// every frame carries the payload twice, on the hottest path in the system. A
// version negotiated once at connection time costs nothing per frame and is how
// real protocols evolve.
//
// Negotiation is a `protocol` query parameter on the upgrade request
// (`/ws?token=...&protocol=2`), or the `Sec-WebSocket-Protocol` header. Absent or
// unparseable, the connection gets version 1 — so every existing client keeps
// working with no change at all.

enum class Version : std::uint8_t {
    kV1 = 1,  // legacy {type, data}
    kV2 = 2,  // full envelope
};

// What a client gets when it does not ask. Must remain kV1: this is the
// compatibility contract.
inline constexpr Version kDefaultVersion = Version::kV1;

// The newest version this build speaks.
inline constexpr Version kCurrentVersion = Version::kV2;

// Query parameter and subprotocol token used for negotiation.
inline constexpr std::string_view kVersionQueryParam = "protocol";

[[nodiscard]] constexpr std::uint8_t to_number(Version version) noexcept {
    return static_cast<std::uint8_t>(version);
}

// Maps a requested version number onto a version this build supports. Anything
// unknown falls back to the default rather than failing the handshake — a client
// asking for a future version gets a working connection, not a dead one.
[[nodiscard]] constexpr Version negotiate(std::optional<int> requested) noexcept {
    if (!requested) {
        return kDefaultVersion;
    }
    switch (*requested) {
        case 1:
            return Version::kV1;
        case 2:
            return Version::kV2;
        default:
            return kDefaultVersion;
    }
}

// Per-frame correlation metadata. All fields optional: a server-initiated frame
// (a broadcast) has no originating client request, while a reply to a client
// command echoes that command's request_id so the client can match them up.
struct Envelope {
    std::string request_id;      // id of the client frame this responds to, if any
    std::string correlation_id;  // trace id / request id spanning the whole operation
};

// Serialises one frame in `version`.
//
// `event` is the canonical event name from rtc/realtime/events.hpp. Returns the
// JSON text ready to hand to the socket.
[[nodiscard]] std::string encode(Version version,
                                 std::string_view event,
                                 const nlohmann::json& payload,
                                 const Envelope& envelope = {});

// Parses an inbound client frame.
//
// Accepts both shapes so a v2 client and a v1 client can be handled by the same
// code path:
//   v1: {"type": "message.send", "data": {...}}
//   v2: {"event": "message.send", "payload": {...}, "request_id": "..."}
struct InboundFrame {
    std::string event;
    nlohmann::json payload = nlohmann::json::object();
    std::string request_id;
    std::string correlation_id;
    // Version inferred from the frame's shape, which lets a client upgrade
    // mid-connection simply by sending the newer form.
    Version version = Version::kV1;
};

// Returns nullopt when the text is not valid JSON or carries no event name.
[[nodiscard]] std::optional<InboundFrame> decode(std::string_view text);

}  // namespace rtc::realtime::protocol
