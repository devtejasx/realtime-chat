#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <crow/websocket.h>
#include <nlohmann/json.hpp>

#include "rtc/realtime/cluster_bus.hpp"
#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/realtime/protocol.hpp"
#include "rtc/realtime/room_manager.hpp"
#include "rtc/realtime/session.hpp"
#include "rtc/realtime/session_manager.hpp"

namespace rtc::realtime {

// Central hub for real-time delivery. Owns the session and room registries and
// implements IEventBroadcaster, so the service layer fans out through the same
// object the WebSocket controller feeds. Every method is thread-safe.
//
// Frame encoding is per-recipient: each session carries the protocol version it
// negotiated at handshake time (see rtc/realtime/protocol.hpp), and a frame is
// serialised once *per distinct version* present among the recipients — at most
// twice — rather than once per connection.
//
// Horizontal scaling
// ------------------
// WebSocket connections are pinned to the instance that accepted them, so local
// delivery alone is only complete in a single-replica deployment. When a cluster
// bus is attached, every fan-out is also published to the cluster, and peer
// instances deliver it to their own connections. The two paths are deliberately
// separate methods:
//
//   publish()/broadcast_to_room()  — deliver locally *and* forward to the cluster
//   deliver_local()/...            — deliver locally only
//
// The cluster subscriber calls the local-only forms, which is what stops a
// message ping-ponging between instances. (Origin-node filtering in the bus is
// the second, independent guard.)
class ConnectionManager final : public IEventBroadcaster {
public:
    // --- lifecycle (called by the WebSocket controller) ---
    std::shared_ptr<Session> register_session(
        crow::websocket::connection* conn, std::int64_t user_id, std::string username,
        protocol::Version version = protocol::kDefaultVersion);
    std::shared_ptr<Session> unregister_session(crow::websocket::connection* conn);

    // Attaches the cross-instance bus and registers the inbound handlers. Call
    // once at startup, before the bus is started. Without it the manager operates
    // in single-instance mode, which is correct for one replica.
    void set_cluster_bus(IClusterBus& bus);

    // --- IEventBroadcaster ---
    // Delivers to every live session of every listed user, then forwards to the
    // cluster so replicas reach their own connections.
    void publish(const std::vector<std::int64_t>& user_ids, std::string_view type,
                 const nlohmann::json& data) override;

    // --- direct / room delivery ---
    void send_event(crow::websocket::connection* conn, std::string_view type,
                    const nlohmann::json& data,
                    const protocol::Envelope& envelope = protocol::Envelope{});

    void broadcast_to_room(std::int64_t conversation_id, std::string_view type,
                           const nlohmann::json& data,
                           crow::websocket::connection* exclude = nullptr);

    // --- local-only variants (used by the cluster subscriber) ---
    void deliver_local(const std::vector<std::int64_t>& user_ids, std::string_view type,
                       const nlohmann::json& data);
    void deliver_local_to_room(std::int64_t conversation_id, std::string_view type,
                               const nlohmann::json& data,
                               crow::websocket::connection* exclude = nullptr);

    [[nodiscard]] SessionManager& sessions() noexcept { return sessions_; }
    [[nodiscard]] RoomManager& rooms() noexcept { return rooms_; }
    [[nodiscard]] const IClusterBus* cluster_bus() const noexcept { return cluster_; }

    // Builds the legacy (v1) wire envelope for an event. Retained with its
    // original signature and behaviour: it is part of the tested surface, and v1
    // is a frozen format.
    [[nodiscard]] static std::string make_envelope(std::string_view type,
                                                   const nlohmann::json& data);

private:
    // Lazily serialises `type`/`data` for a protocol version, caching the result
    // so a fan-out encodes at most once per distinct version among recipients.
    class FrameCache {
    public:
        FrameCache(std::string_view type, const nlohmann::json& data,
                   const protocol::Envelope& envelope) noexcept
            : type_(type), data_(data), envelope_(envelope) {}

        [[nodiscard]] const std::string& for_version(protocol::Version version);

    private:
        std::string_view type_;
        const nlohmann::json& data_;
        const protocol::Envelope& envelope_;
        std::string v1_;
        std::string v2_;
        bool has_v1_ = false;
        bool has_v2_ = false;
    };

    void send_to_sessions(const std::vector<std::shared_ptr<Session>>& sessions,
                          std::string_view type, const nlohmann::json& data,
                          const protocol::Envelope& envelope,
                          crow::websocket::connection* exclude);

    static void send_raw(crow::websocket::connection* conn, const std::string& payload);

    SessionManager sessions_;
    RoomManager rooms_;
    IClusterBus* cluster_ = nullptr;  // non-owning; Application owns the bus
};

}  // namespace rtc::realtime
