#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <crow/websocket.h>

namespace rtc::realtime {

// Authenticated identity attached to a WebSocket connection during the
// handshake (onaccept). Stored as the connection's userdata so onopen/onmessage
// /onclose can recover who the peer is without re-verifying the token.
struct AuthContext {
    std::int64_t user_id = 0;
    std::string username;
};

// A live, authenticated WebSocket session. Held via shared_ptr so it can be
// referenced from the session map, the room map, and the heartbeat monitor
// without lifetime hazards. `last_activity_ms` is updated on every inbound
// frame and read by the heartbeat monitor.
struct Session {
    crow::websocket::connection* connection = nullptr;
    std::int64_t user_id = 0;
    std::string username;
    std::atomic<std::int64_t> last_activity_ms{0};

    Session(crow::websocket::connection* conn, std::int64_t uid, std::string uname,
            std::int64_t now_ms)
        : connection(conn), user_id(uid), username(std::move(uname)), last_activity_ms(now_ms) {}
};

}  // namespace rtc::realtime
