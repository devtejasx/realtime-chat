#include "rtc/services/notification_service.hpp"

#include <string>

#include "rtc/dto/notification_dto.hpp"
#include "rtc/realtime/events.hpp"

namespace rtc::services {
namespace {

// Derives a human-readable title/body for external push from the notification.
[[nodiscard]] std::pair<std::string, std::string> push_text(models::NotificationType type) {
    switch (type) {
        case models::NotificationType::kNewMessage:
            return {"New message", "You have a new message"};
        case models::NotificationType::kMention:
            return {"You were mentioned", "Someone mentioned you"};
        case models::NotificationType::kAddedToGroup:
            return {"Added to a group", "You were added to a group"};
        case models::NotificationType::kRemovedFromGroup:
            return {"Removed from a group", "You were removed from a group"};
        case models::NotificationType::kReactionAdded:
            return {"New reaction", "Someone reacted to your message"};
        case models::NotificationType::kProfileUpdated:
            return {"Profile updated", "A profile was updated"};
    }
    return {"Notification", ""};
}

}  // namespace

models::Notification NotificationService::create(std::int64_t user_id,
                                                 models::NotificationType type,
                                                 const nlohmann::json& payload) {
    const models::Notification notification = repository_.create(user_id, type, payload);
    metrics_.increment("rtc_notifications_total");

    // In-app delivery over WebSocket to the recipient's live sessions.
    broadcaster_.publish({user_id}, realtime::events::kNotification,
                         dto::NotificationResponse::from(notification).to_json());

    // External push happens off the request path.
    const auto [title, body] = push_text(type);
    executor_.submit([this, user_id, title, body, payload] {
        push_.send(user_id, title, body, payload);
    });
    return notification;
}

std::vector<models::Notification> NotificationService::list(std::int64_t user_id,
                                                            const dto::Pagination& page,
                                                            bool unread_only) {
    return repository_.list_for_user(user_id, page, unread_only);
}

std::int64_t NotificationService::unread_count(std::int64_t user_id) {
    return repository_.unread_count(user_id);
}

bool NotificationService::mark_read(std::int64_t id, std::int64_t user_id) {
    return repository_.mark_read(id, user_id);
}

std::int64_t NotificationService::mark_all_read(std::int64_t user_id) {
    return repository_.mark_all_read(user_id);
}

bool NotificationService::remove(std::int64_t id, std::int64_t user_id) {
    return repository_.remove(id, user_id);
}

}  // namespace rtc::services
