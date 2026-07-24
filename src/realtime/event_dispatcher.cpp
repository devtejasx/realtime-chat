#include "rtc/realtime/event_dispatcher.hpp"

#include <chrono>
#include <exception>
#include <string>

#include <nlohmann/json.hpp>

#include "rtc/dto/message_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/realtime/events.hpp"

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

void EventDispatcher::on_open(crow::websocket::connection& conn, std::int64_t user_id,
                              const std::string& username) {
    auto session = connections_.register_session(&conn, user_id, username);
    session->last_activity_ms.store(now_ms(), std::memory_order_relaxed);

    try {
        // Subscribe to the rooms of every conversation this user belongs to so
        // typing/presence signals reach them without per-event DB lookups.
        connections_.rooms().join_many(conversations_.conversation_ids(user_id), &conn);

        // Presence: announce online to peers only on the first live session.
        if (presence_.on_connect(user_id)) {
            connections_.publish(conversations_.peer_ids(user_id),
                                 realtime::events::kPresenceUpdate,
                                 nlohmann::json{{"user_id", user_id}, {"status", "online"}});
        }
    } catch (const std::exception& ex) {
        RTC_LOG_WARN("ws on_open side effects failed for user {}: {}", user_id, ex.what());
    }

    connections_.send_event(&conn, realtime::events::kReady,
                            nlohmann::json{{"user_id", user_id}, {"username", username}});
}

void EventDispatcher::on_message(crow::websocket::connection& conn, const std::string& data) {
    connections_.sessions().touch(&conn, now_ms());

    const auto session = connections_.sessions().get(&conn);
    if (!session) {
        return;  // frame from an unregistered connection; ignore
    }
    const std::int64_t user_id = session->user_id;

    nlohmann::json envelope = nlohmann::json::parse(data, nullptr, false);
    if (envelope.is_discarded() || !envelope.is_object() || !envelope.contains("type")) {
        send_error(conn, "validation_error", "Malformed event envelope");
        return;
    }
    const std::string type = envelope.value("type", std::string{});
    const nlohmann::json payload =
        envelope.contains("data") ? envelope["data"] : nlohmann::json::object();

    try {
        if (type == realtime::events::kClientPing) {
            connections_.send_event(&conn, realtime::events::kPong, nlohmann::json::object());
        } else if (type == realtime::events::kClientSendMessage) {
            handle_send_message(user_id, payload);
        } else if (type == realtime::events::kClientTypingStart) {
            handle_typing(conn, user_id, session->username, payload, /*starting=*/true);
        } else if (type == realtime::events::kClientTypingStop) {
            handle_typing(conn, user_id, session->username, payload, /*starting=*/false);
        } else if (type == realtime::events::kClientMarkDelivered) {
            handle_mark_delivered(user_id, payload);
        } else if (type == realtime::events::kClientMarkRead) {
            handle_mark_read(user_id, payload);
        } else {
            send_error(conn, "validation_error", "Unknown event type");
        }
    } catch (const errors::AppException& ex) {
        send_error(conn, ex.code(), ex.message());
    } catch (const std::exception& ex) {
        RTC_LOG_ERROR("ws handler error (type={}): {}", type, ex.what());
        send_error(conn, "internal_error", "Failed to process event");
    }
}

void EventDispatcher::on_close(crow::websocket::connection& conn) {
    auto session = connections_.unregister_session(&conn);
    if (!session) {
        return;
    }
    try {
        if (presence_.on_disconnect(session->user_id)) {
            connections_.publish(conversations_.peer_ids(session->user_id),
                                 realtime::events::kPresenceUpdate,
                                 nlohmann::json{{"user_id", session->user_id},
                                                {"status", "offline"}});
        }
    } catch (const std::exception& ex) {
        RTC_LOG_WARN("ws on_close side effects failed for user {}: {}", session->user_id,
                     ex.what());
    }
}

void EventDispatcher::handle_send_message(std::int64_t user_id, const nlohmann::json& data) {
    // Reuse the exact REST DTO + service path — no duplicated logic.
    auto request = dto::SendMessageRequest::from_json(data);
    (void) messages_.send(user_id, request);  // persists then broadcasts to all participants
}

void EventDispatcher::handle_typing(crow::websocket::connection& conn, std::int64_t user_id,
                                    const std::string& username, const nlohmann::json& data,
                                    bool starting) {
    const std::int64_t conversation_id = require_int(data, "conversation_id");
    // Authorize membership (throws if the user isn't a participant), then fan out
    // to the online room members only. Typing is never persisted.
    (void) conversations_.get(user_id, conversation_id);
    connections_.broadcast_to_room(
        conversation_id,
        starting ? realtime::events::kTypingStart : realtime::events::kTypingStop,
        nlohmann::json{{"conversation_id", conversation_id},
                       {"user_id", user_id},
                       {"username", username}},
        &conn);
}

void EventDispatcher::handle_mark_delivered(std::int64_t user_id, const nlohmann::json& data) {
    receipts_.mark_delivered(user_id, require_int(data, "message_id"));
}

void EventDispatcher::handle_mark_read(std::int64_t user_id, const nlohmann::json& data) {
    receipts_.mark_read(user_id, require_int(data, "conversation_id"),
                        require_int(data, "up_to_message_id"));
}

void EventDispatcher::send_error(crow::websocket::connection& conn, std::string_view code,
                                 std::string_view message) {
    connections_.send_event(&conn, realtime::events::kError,
                            nlohmann::json{{"code", code}, {"message", message}});
}

}  // namespace rtc::realtime
