#include "rtc/services/notification_dispatcher.hpp"

#include <exception>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/logging/logger.hpp"

namespace rtc::services {
namespace {

// Runs a notification action, isolating any failure (the noexcept contract).
template <typename Fn>
void guarded(const char* what, Fn&& fn) noexcept {
    try {
        fn();
    } catch (const std::exception& ex) {
        RTC_LOG_WARN("Notification dispatch '{}' failed: {}", what, ex.what());
    } catch (...) {
        RTC_LOG_WARN("Notification dispatch '{}' failed (unknown)", what);
    }
}

}  // namespace

void NotificationDispatcher::new_message(std::int64_t sender_id,
                                         std::int64_t conversation_id,
                                         std::int64_t message_id,
                                         const std::vector<std::int64_t>& recipient_ids) noexcept {
    guarded("new_message", [&] {
        const nlohmann::json payload{{"conversation_id", conversation_id},
                                     {"message_id", message_id},
                                     {"sender_id", sender_id}};
        for (const std::int64_t recipient : recipient_ids) {
            if (recipient == sender_id) {
                continue;  // never notify the sender of their own message
            }
            notifications_.create(recipient, models::NotificationType::kNewMessage, payload);
        }
    });
}

void NotificationDispatcher::added_to_group(std::int64_t conversation_id,
                                            std::int64_t added_user_id,
                                            std::int64_t actor_id) noexcept {
    guarded("added_to_group", [&] {
        notifications_.create(
            added_user_id,
            models::NotificationType::kAddedToGroup,
            nlohmann::json{{"conversation_id", conversation_id}, {"actor_id", actor_id}});
    });
}

void NotificationDispatcher::removed_from_group(std::int64_t conversation_id,
                                                std::int64_t removed_user_id,
                                                std::int64_t actor_id) noexcept {
    guarded("removed_from_group", [&] {
        notifications_.create(
            removed_user_id,
            models::NotificationType::kRemovedFromGroup,
            nlohmann::json{{"conversation_id", conversation_id}, {"actor_id", actor_id}});
    });
}

void NotificationDispatcher::reaction_added(std::int64_t message_author_id,
                                            std::int64_t message_id,
                                            std::int64_t reactor_id,
                                            std::string_view emoji) noexcept {
    guarded("reaction_added", [&] {
        if (message_author_id == reactor_id) {
            return;  // don't notify about reacting to your own message
        }
        notifications_.create(message_author_id,
                              models::NotificationType::kReactionAdded,
                              nlohmann::json{{"message_id", message_id},
                                             {"reactor_id", reactor_id},
                                             {"emoji", std::string(emoji)}});
    });
}

void NotificationDispatcher::profile_updated(std::int64_t user_id) noexcept {
    // Profile updates are currently not surfaced as recipient notifications;
    // the hook exists so future policy (e.g. notifying contacts) needs no
    // producer change.
    (void) user_id;
}

}  // namespace rtc::services
