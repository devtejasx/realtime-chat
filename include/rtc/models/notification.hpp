#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// The kinds of notification the system emits. Stored as a string so new kinds
// can be added without a schema migration.
enum class NotificationType {
    kNewMessage,
    kMention,
    kAddedToGroup,
    kRemovedFromGroup,
    kReactionAdded,
    kProfileUpdated,
};

[[nodiscard]] constexpr std::string_view to_string(NotificationType type) noexcept {
    switch (type) {
        case NotificationType::kNewMessage:
            return "new_message";
        case NotificationType::kMention:
            return "mention";
        case NotificationType::kAddedToGroup:
            return "added_to_group";
        case NotificationType::kRemovedFromGroup:
            return "removed_from_group";
        case NotificationType::kReactionAdded:
            return "reaction_added";
        case NotificationType::kProfileUpdated:
            return "profile_updated";
    }
    return "new_message";
}

// Persistent notification (row in `notifications`). `payload` carries
// type-specific context (conversation id, message id, actor, ...).
struct Notification {
    std::int64_t id = 0;
    std::int64_t user_id = 0;  // recipient
    NotificationType type = NotificationType::kNewMessage;
    nlohmann::json payload = nlohmann::json::object();
    std::optional<utils::TimePoint> read_at;
    utils::TimePoint created_at{};

    [[nodiscard]] bool is_read() const noexcept { return read_at.has_value(); }
};

}  // namespace rtc::models
