#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <crow/websocket.h>

namespace rtc::realtime {

// Thread-safe mapping between conversation "rooms" and the connections
// subscribed to them.
//
// Rooms give ephemeral, DB-free fan-out for high-frequency signals (typing
// indicators, presence) to the *online* participants of a conversation. On
// connect a session joins the rooms of its conversations; on disconnect all its
// memberships are removed in one call. Authoritative membership still lives in
// the database — rooms are a routing cache, not the source of truth.
class RoomManager {
public:
    void join(std::int64_t conversation_id, crow::websocket::connection* conn);
    void join_many(const std::vector<std::int64_t>& conversation_ids,
                   crow::websocket::connection* conn);
    void leave(std::int64_t conversation_id, crow::websocket::connection* conn);

    // Removes the connection from every room it had joined.
    void leave_all(crow::websocket::connection* conn);

    [[nodiscard]] std::vector<crow::websocket::connection*> connections_in_room(
        std::int64_t conversation_id) const;

    [[nodiscard]] std::size_t room_count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::int64_t, std::unordered_set<crow::websocket::connection*>> rooms_;
    std::unordered_map<crow::websocket::connection*, std::unordered_set<std::int64_t>> membership_;
};

}  // namespace rtc::realtime
