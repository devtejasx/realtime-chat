#pragma once

#include <crow/websocket.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "rtc/realtime/protocol.hpp"

namespace rtc::realtime {

// Authenticated identity attached to a WebSocket connection during the
// handshake (onaccept). Stored as the connection's userdata so onopen/onmessage
// /onclose can recover who the peer is without re-verifying the token.
struct AuthContext {
    std::int64_t user_id = 0;
    std::string username;
    // Wire protocol negotiated from the upgrade request. Decided at handshake
    // time because that is the only point the query string is available.
    protocol::Version protocol_version = protocol::kDefaultVersion;
};

// A live, authenticated WebSocket session. Held via shared_ptr so it can be
// referenced from the session map, the room map, and the heartbeat monitor
// without lifetime hazards. `last_activity_ms` is updated on every inbound
// frame and read by the heartbeat monitor.
//
// `protocol_version` is immutable for the connection's lifetime and is what the
// broadcast path consults to encode each frame in the shape this peer expects.
struct Session {
    crow::websocket::connection* connection = nullptr;
    std::int64_t user_id = 0;
    std::string username;
    protocol::Version protocol_version = protocol::kDefaultVersion;
    std::atomic<std::int64_t> last_activity_ms{0};

    Session(crow::websocket::connection* conn,
            std::int64_t uid,
            std::string uname,
            std::int64_t now_ms,
            protocol::Version version = protocol::kDefaultVersion)
        : connection(conn),
          user_id(uid),
          username(std::move(uname)),
          protocol_version(version),
          last_activity_ms(now_ms) {}
};

}  // namespace rtc::realtime
