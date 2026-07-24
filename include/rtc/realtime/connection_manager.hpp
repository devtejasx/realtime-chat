#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <crow/websocket.h>
#include <nlohmann/json.hpp>

#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/realtime/room_manager.hpp"
#include "rtc/realtime/session.hpp"
#include "rtc/realtime/session_manager.hpp"

namespace rtc::realtime {

// Central hub for real-time delivery. Owns the session and room registries and
// implements IEventBroadcaster, so the service layer fans out through the same
// object the WebSocket controller feeds. All send paths serialise a uniform
// {type, data} envelope. Every method is thread-safe.
class ConnectionManager final : public IEventBroadcaster {
public:
    // --- lifecycle (called by the WebSocket controller) ---
    std::shared_ptr<Session> register_session(crow::websocket::connection* conn,
                                              std::int64_t user_id, std::string username);
    std::shared_ptr<Session> unregister_session(crow::websocket::connection* conn);

    // --- IEventBroadcaster ---
    void publish(const std::vector<std::int64_t>& user_ids, std::string_view type,
                 const nlohmann::json& data) override;

    // --- direct / room delivery ---
    void send_event(crow::websocket::connection* conn, std::string_view type,
                    const nlohmann::json& data);
    void broadcast_to_room(std::int64_t conversation_id, std::string_view type,
                           const nlohmann::json& data,
                           crow::websocket::connection* exclude = nullptr);

    [[nodiscard]] SessionManager& sessions() noexcept { return sessions_; }
    [[nodiscard]] RoomManager& rooms() noexcept { return rooms_; }

    // Builds the wire envelope for an event (exposed for testing).
    [[nodiscard]] static std::string make_envelope(std::string_view type,
                                                   const nlohmann::json& data);

private:
    static void send_raw(crow::websocket::connection* conn, const std::string& payload);

    SessionManager sessions_;
    RoomManager rooms_;
};

}  // namespace rtc::realtime
