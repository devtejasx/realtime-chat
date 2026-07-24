#include "rtc/realtime/connection_manager.hpp"

#include <utility>

namespace rtc::realtime {

std::string ConnectionManager::make_envelope(std::string_view type, const nlohmann::json& data) {
    return nlohmann::json{{"type", type}, {"data", data}}.dump();
}

void ConnectionManager::send_raw(crow::websocket::connection* conn, const std::string& payload) {
    if (conn != nullptr) {
        // Crow serialises writes per connection internally, so this is safe to
        // call from any thread (service broadcast, heartbeat, I/O handler).
        conn->send_text(payload);
    }
}

std::shared_ptr<Session> ConnectionManager::register_session(crow::websocket::connection* conn,
                                                             std::int64_t user_id,
                                                             std::string username) {
    return sessions_.add(conn, user_id, std::move(username));
}

std::shared_ptr<Session> ConnectionManager::unregister_session(
    crow::websocket::connection* conn) {
    rooms_.leave_all(conn);
    return sessions_.remove(conn);
}

void ConnectionManager::publish(const std::vector<std::int64_t>& user_ids, std::string_view type,
                                const nlohmann::json& data) {
    // Serialise once, then deliver to every live session of every target user.
    const std::string payload = make_envelope(type, data);
    for (const std::int64_t user_id : user_ids) {
        for (auto* conn : sessions_.connections_for_user(user_id)) {
            send_raw(conn, payload);
        }
    }
}

void ConnectionManager::send_event(crow::websocket::connection* conn, std::string_view type,
                                   const nlohmann::json& data) {
    send_raw(conn, make_envelope(type, data));
}

void ConnectionManager::broadcast_to_room(std::int64_t conversation_id, std::string_view type,
                                          const nlohmann::json& data,
                                          crow::websocket::connection* exclude) {
    const std::string payload = make_envelope(type, data);
    for (auto* conn : rooms_.connections_in_room(conversation_id)) {
        if (conn != exclude) {
            send_raw(conn, payload);
        }
    }
}

}  // namespace rtc::realtime
