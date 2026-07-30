#pragma once

#include <crow/websocket.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    // Registers a session. Returns the created Session. `version` is the wire
    // protocol negotiated during the handshake; it defaults to the legacy version
    // so existing callers (and tests) are unaffected.
    std::shared_ptr<Session> add(crow::websocket::connection* conn,
                                 std::int64_t user_id,
                                 std::string username,
                                 protocol::Version version = protocol::kDefaultVersion);

    // Removes and returns the session for `conn`, or nullptr if unknown.
    std::shared_ptr<Session> remove(crow::websocket::connection* conn);

    [[nodiscard]] std::shared_ptr<Session> get(crow::websocket::connection* conn) const;

    // All live connections for a user (possibly empty).
    [[nodiscard]] std::vector<crow::websocket::connection*> connections_for_user(
        std::int64_t user_id) const;

    // Sessions (not bare connections) for a user. The broadcast path needs these
    // because encoding a frame depends on each peer's negotiated protocol version.
    [[nodiscard]] std::vector<std::shared_ptr<Session>> sessions_for_user(
        std::int64_t user_id) const;

    // Batch connection -> session resolution under a single lock. Used by room
    // broadcasts, which start from a set of connections: looking each one up
    // separately would take and release the mutex once per recipient.
    [[nodiscard]] std::vector<std::shared_ptr<Session>> sessions_for(
        const std::vector<crow::websocket::connection*>& connections) const;

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
