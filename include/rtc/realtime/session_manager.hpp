#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <crow/websocket.h>

#include "rtc/realtime/session.hpp"

namespace rtc::realtime {

// Thread-safe registry of live WebSocket sessions.
//
// Maintains two indexes: connection -> Session (for per-frame lookups) and
// user_id -> connections (for fan-out to all of a user's devices). All access
// is guarded by a single mutex; operations are O(1) amortised. Designed to hold
// thousands of concurrent sessions.
class SessionManager {
public:
    // Registers a session. Returns the created Session.
    std::shared_ptr<Session> add(crow::websocket::connection* conn, std::int64_t user_id,
                                 std::string username);

    // Removes and returns the session for `conn`, or nullptr if unknown.
    std::shared_ptr<Session> remove(crow::websocket::connection* conn);

    [[nodiscard]] std::shared_ptr<Session> get(crow::websocket::connection* conn) const;

    // All live connections for a user (possibly empty).
    [[nodiscard]] std::vector<crow::websocket::connection*> connections_for_user(
        std::int64_t user_id) const;

    // Snapshot of every live session (for the heartbeat sweep).
    [[nodiscard]] std::vector<std::shared_ptr<Session>> snapshot() const;

    // Updates the activity timestamp for a connection.
    void touch(crow::websocket::connection* conn, std::int64_t now_ms);

    [[nodiscard]] std::size_t session_count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<crow::websocket::connection*, std::shared_ptr<Session>> by_connection_;
    std::unordered_map<std::int64_t, std::unordered_set<crow::websocket::connection*>> by_user_;
};

}  // namespace rtc::realtime
