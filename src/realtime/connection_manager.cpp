#include "rtc/realtime/connection_manager.hpp"

#include <utility>

#include "rtc/logging/logger.hpp"

namespace rtc::realtime {
namespace {

// Keys used in cluster-bus message bodies. Kept private to this file: the wire
// shape of a cluster message is an internal contract between instances of the
// same build, not a public API.
constexpr const char* kFieldEvent = "event";
constexpr const char* kFieldData = "data";
constexpr const char* kFieldUserIds = "user_ids";
constexpr const char* kFieldConversationId = "conversation_id";

}  // namespace

const std::string& ConnectionManager::FrameCache::for_version(protocol::Version version) {
    if (version == protocol::Version::kV2) {
        if (!has_v2_) {
            v2_ = protocol::encode(protocol::Version::kV2, type_, data_, envelope_);
            has_v2_ = true;
        }
        return v2_;
    }
    if (!has_v1_) {
        v1_ = protocol::encode(protocol::Version::kV1, type_, data_, envelope_);
        has_v1_ = true;
    }
    return v1_;
}

std::string ConnectionManager::make_envelope(std::string_view type, const nlohmann::json& data) {
    return protocol::encode(protocol::Version::kV1, type, data);
}

void ConnectionManager::send_raw(crow::websocket::connection* conn, const std::string& payload) {
    if (conn != nullptr) {
        // Crow serialises writes per connection internally, so this is safe to
        // call from any thread (service broadcast, heartbeat, I/O handler,
        // cluster-bus subscriber).
        conn->send_text(payload);
    }
}

std::shared_ptr<Session> ConnectionManager::register_session(crow::websocket::connection* conn,
                                                             std::int64_t user_id,
                                                             std::string username,
                                                             protocol::Version version) {
    return sessions_.add(conn, user_id, std::move(username), version);
}

std::shared_ptr<Session> ConnectionManager::unregister_session(crow::websocket::connection* conn) {
    rooms_.leave_all(conn);
    return sessions_.remove(conn);
}

void ConnectionManager::set_cluster_bus(IClusterBus& bus) {
    cluster_ = &bus;

    // Inbound cluster messages are delivered *locally only*. Re-publishing them
    // would loop the message around the cluster indefinitely.
    bus.subscribe(cluster_channels::kUserBroadcast,
                  [this](std::string_view /*origin*/, const nlohmann::json& body) {
                      const auto event = body.find(kFieldEvent);
                      const auto users = body.find(kFieldUserIds);
                      if (event == body.end() || !event->is_string() || users == body.end() ||
                          !users->is_array()) {
                          return;
                      }
                      const auto data_it = body.find(kFieldData);
                      deliver_local(users->get<std::vector<std::int64_t>>(),
                                    event->get<std::string>(),
                                    data_it != body.end() ? *data_it : nlohmann::json::object());
                  });

    bus.subscribe(cluster_channels::kRoomBroadcast,
                  [this](std::string_view /*origin*/, const nlohmann::json& body) {
                      const auto event = body.find(kFieldEvent);
                      const auto room = body.find(kFieldConversationId);
                      if (event == body.end() || !event->is_string() || room == body.end() ||
                          !room->is_number_integer()) {
                          return;
                      }
                      const auto data_it = body.find(kFieldData);
                      // No `exclude`: the excluded connection lives on the
                      // originating instance, which already handled it locally.
                      deliver_local_to_room(
                          room->get<std::int64_t>(),
                          event->get<std::string>(),
                          data_it != body.end() ? *data_it : nlohmann::json::object());
                  });

    RTC_LOG_INFO("Cluster fan-out enabled (node '{}')", bus.node_id());
}

void ConnectionManager::send_to_sessions(const std::vector<std::shared_ptr<Session>>& sessions,
                                         std::string_view type,
                                         const nlohmann::json& data,
                                         const protocol::Envelope& envelope,
                                         crow::websocket::connection* exclude) {
    // One FrameCache per fan-out: the payload is serialised at most once per
    // distinct protocol version among the recipients, not once per connection.
    FrameCache frames(type, data, envelope);
    for (const auto& session : sessions) {
        if (!session || session->connection == nullptr || session->connection == exclude) {
            continue;
        }
        send_raw(session->connection, frames.for_version(session->protocol_version));
    }
}

void ConnectionManager::deliver_local(const std::vector<std::int64_t>& user_ids,
                                      std::string_view type,
                                      const nlohmann::json& data) {
    const protocol::Envelope envelope{};
    for (const std::int64_t user_id : user_ids) {
        send_to_sessions(sessions_.sessions_for_user(user_id),
                         type,
                         data,
                         envelope,
                         /*exclude=*/nullptr);
    }
}

void ConnectionManager::publish(const std::vector<std::int64_t>& user_ids,
                                std::string_view type,
                                const nlohmann::json& data) {
    deliver_local(user_ids, type, data);

    if (cluster_ != nullptr) {
        // Peers deliver to their own connections. Fire-and-forget: publish() is
        // noexcept and a cluster failure degrades delivery without failing the
        // action that produced the event.
        cluster_->publish(
            cluster_channels::kUserBroadcast,
            nlohmann::json{{kFieldEvent, type}, {kFieldUserIds, user_ids}, {kFieldData, data}});
    }
}

void ConnectionManager::send_event(crow::websocket::connection* conn,
                                   std::string_view type,
                                   const nlohmann::json& data,
                                   const protocol::Envelope& envelope) {
    // A direct send targets one known local connection, so it is never forwarded
    // to the cluster.
    const auto session = sessions_.get(conn);
    const protocol::Version version =
        session ? session->protocol_version : protocol::kDefaultVersion;
    send_raw(conn, protocol::encode(version, type, data, envelope));
}

void ConnectionManager::deliver_local_to_room(std::int64_t conversation_id,
                                              std::string_view type,
                                              const nlohmann::json& data,
                                              crow::websocket::connection* exclude) {
    send_to_sessions(sessions_.sessions_for(rooms_.connections_in_room(conversation_id)),
                     type,
                     data,
                     protocol::Envelope{},
                     exclude);
}

void ConnectionManager::broadcast_to_room(std::int64_t conversation_id,
                                          std::string_view type,
                                          const nlohmann::json& data,
                                          crow::websocket::connection* exclude) {
    deliver_local_to_room(conversation_id, type, data, exclude);

    if (cluster_ != nullptr) {
        cluster_->publish(
            cluster_channels::kRoomBroadcast,
            nlohmann::json{
                {kFieldEvent, type}, {kFieldConversationId, conversation_id}, {kFieldData, data}});
    }
}

}  // namespace rtc::realtime
