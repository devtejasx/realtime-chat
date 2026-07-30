#include "rtc/realtime/event_dispatcher.hpp"

#include <chrono>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/dto/message_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/realtime/events.hpp"
#include "rtc/tracing/scoped_span.hpp"

namespace rtc::realtime {
namespace {

[[nodiscard]] std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] std::int64_t require_int(const nlohmann::json& data, const char* field) {
    const auto it = data.find(field);
    if (it == data.end() || !it->is_number_integer()) {
        throw errors::ValidationException(std::string("Missing integer field: ") + field);
    }
    return it->get<std::int64_t>();
}

}  // namespace

bool EventDispatcher::enabled(features::Feature feature) const noexcept {
    // No flag store attached => everything on. Keeps the dispatcher usable
    // standalone (and in the existing unit tests) with no behaviour change.
    return flags_ == nullptr || flags_->is_enabled(feature);
}

void EventDispatcher::on_open(crow::websocket::connection& conn,
                              std::int64_t user_id,
                              const std::string& username,
                              protocol::Version version) {
    auto session = connections_.register_session(&conn, user_id, username, version);
    session->last_activity_ms.store(now_ms(), std::memory_order_relaxed);

    try {
        // Subscribe to the rooms of every conversation this user belongs to so
        // typing/presence signals reach them without per-event DB lookups.
        connections_.rooms().join_many(conversations_.conversation_ids(user_id), &conn);

        // Presence: announce online to peers only on the first live session.
        if (enabled(features::Feature::kPresence) && presence_.on_connect(user_id)) {
            connections_.publish(conversations_.peer_ids(user_id),
                                 realtime::events::kPresenceUpdate,
                                 nlohmann::json{{"user_id", user_id}, {"status", "online"}});
            if (publisher_ != nullptr) {
                const ::rtc::events::UserOnline user_online_event{
                    .user_id = user_id,
                    .username = username,
                    .session_count = connections_.sessions().sessions_for_user(user_id).size(),
                };
                publisher_->publish(user_online_event.to_event());
            }
        }
    } catch (const std::exception& ex) {
        RTC_LOG_WARN("ws on_open side effects failed for user {}: {}", user_id, ex.what());
    }

    // The ready frame tells the client which protocol it actually got, so a
    // client that asked for v2 can verify it rather than assume.
    connections_.send_event(&conn,
                            realtime::events::kReady,
                            nlohmann::json{{"user_id", user_id},
                                           {"username", username},
                                           {"protocol_version", protocol::to_number(version)}});
}

void EventDispatcher::on_message(crow::websocket::connection& conn, const std::string& data) {
    connections_.sessions().touch(&conn, now_ms());

    const auto session = connections_.sessions().get(&conn);
    if (!session) {
        return;  // frame from an unregistered connection; ignore
    }
    const std::int64_t user_id = session->user_id;

    const auto frame = protocol::decode(data);
    if (!frame) {
        send_error(conn, "validation_error", "Malformed event envelope", protocol::Envelope{});
        return;
    }
    // Echo the client's ids back on any reply so a v2 client can match request to
    // response; a v1 client simply has none and the fields stay empty.
    const protocol::Envelope envelope{frame->request_id, frame->correlation_id};
    const std::string& type = frame->event;
    const nlohmann::json& payload = frame->payload;

    // One span per inbound frame, parented to nothing (a WebSocket frame is its
    // own operation), so the work it triggers downstream is attributable.
    auto scope = tracing::ws_scope(type);
    scope.span().set_attribute("rtc.user_id", user_id);
    if (!frame->request_id.empty()) {
        scope.span().set_attribute("rtc.request_id", frame->request_id);
    }

    try {
        if (type == realtime::events::kClientPing) {
            connections_.send_event(
                &conn, realtime::events::kPong, nlohmann::json::object(), envelope);
        } else if (type == realtime::events::kClientSendMessage) {
            handle_send_message(user_id, payload);
        } else if (type == realtime::events::kClientTypingStart) {
            if (enabled(features::Feature::kTyping)) {
                handle_typing(conn, user_id, session->username, payload, /*starting=*/true);
            }
        } else if (type == realtime::events::kClientTypingStop) {
            if (enabled(features::Feature::kTyping)) {
                handle_typing(conn, user_id, session->username, payload, /*starting=*/false);
            }
        } else if (type == realtime::events::kClientMarkDelivered) {
            if (enabled(features::Feature::kReadReceipts)) {
                handle_mark_delivered(user_id, payload);
            }
        } else if (type == realtime::events::kClientMarkRead) {
            if (enabled(features::Feature::kReadReceipts)) {
                handle_mark_read(user_id, payload);
            }
        } else {
            send_error(conn, "validation_error", "Unknown event type", envelope);
        }
    } catch (const errors::AppException& ex) {
        scope.span().record_error(ex.message());
        send_error(conn, ex.code(), ex.message(), envelope);
    } catch (const std::exception& ex) {
        scope.span().record_error(ex.what());
        RTC_LOG_ERROR("ws handler error (type={}): {}", type, ex.what());
        send_error(conn, "internal_error", "Failed to process event", envelope);
    }
}

void EventDispatcher::on_close(crow::websocket::connection& conn) {
    auto session = connections_.unregister_session(&conn);
    if (!session) {
        return;
    }
    try {
        if (enabled(features::Feature::kPresence) && presence_.on_disconnect(session->user_id)) {
            connections_.publish(
                conversations_.peer_ids(session->user_id),
                realtime::events::kPresenceUpdate,
                nlohmann::json{{"user_id", session->user_id}, {"status", "offline"}});
            if (publisher_ != nullptr) {
                const ::rtc::events::UserOffline user_offline_event{
                    .user_id = session->user_id,
                    .username = session->username,
                };
                publisher_->publish(user_offline_event.to_event());
            }
        }
    } catch (const std::exception& ex) {
        RTC_LOG_WARN(
            "ws on_close side effects failed for user {}: {}", session->user_id, ex.what());
    }
}

void EventDispatcher::handle_send_message(std::int64_t user_id, const nlohmann::json& data) {
    // Reuse the exact REST DTO + service path — no duplicated logic.
    auto request = dto::SendMessageRequest::from_json(data);
    (void) messages_.send(user_id, request);  // persists then broadcasts to all participants
}

void EventDispatcher::handle_typing(crow::websocket::connection& conn,
                                    std::int64_t user_id,
                                    const std::string& username,
                                    const nlohmann::json& data,
                                    bool starting) {
    const std::int64_t conversation_id = require_int(data, "conversation_id");
    // Authorize membership (throws if the user isn't a participant), then fan out
    // to the online room members only. Typing is never persisted.
    (void) conversations_.get(user_id, conversation_id);
    connections_.broadcast_to_room(
        conversation_id,
        starting ? realtime::events::kTypingStart : realtime::events::kTypingStop,
        nlohmann::json{
            {"conversation_id", conversation_id}, {"user_id", user_id}, {"username", username}},
        &conn);
}

void EventDispatcher::handle_mark_delivered(std::int64_t user_id, const nlohmann::json& data) {
    receipts_.mark_delivered(user_id, require_int(data, "message_id"));
}

void EventDispatcher::handle_mark_read(std::int64_t user_id, const nlohmann::json& data) {
    receipts_.mark_read(
        user_id, require_int(data, "conversation_id"), require_int(data, "up_to_message_id"));
}

void EventDispatcher::send_error(crow::websocket::connection& conn,
                                 std::string_view code,
                                 std::string_view message,
                                 const protocol::Envelope& envelope) {
    connections_.send_event(&conn,
                            realtime::events::kError,
                            nlohmann::json{{"code", code}, {"message", message}},
                            envelope);
}

}  // namespace rtc::realtime
