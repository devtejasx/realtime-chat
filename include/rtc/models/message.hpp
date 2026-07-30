#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// Message kind. "text" is a user message; "system" is a server-generated notice
// (e.g. "X was added"). Only text messages originate from clients.
enum class MessageType {
    kText,
    kSystem,
};

[[nodiscard]] constexpr std::string_view to_string(MessageType type) noexcept {
    switch (type) {
        case MessageType::kText:
            return "text";
        case MessageType::kSystem:
            return "system";
    }
    return "text";
}

[[nodiscard]] constexpr std::optional<MessageType> message_type_from_string(
    std::string_view value) noexcept {
    if (value == "text")
        return MessageType::kText;
    if (value == "system")
        return MessageType::kSystem;
    return std::nullopt;
}

// Persistent message entity (row in `messages`). Edits set `edited_at`;
// deletion is soft via `deleted_at` so receipts and ordering stay stable.
struct Message {
    std::int64_t id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t sender_id = 0;
    MessageType type = MessageType::kText;
    std::string content;
    utils::TimePoint created_at{};
    utils::TimePoint updated_at{};
    std::optional<utils::TimePoint> edited_at;
    std::optional<utils::TimePoint> deleted_at;

    [[nodiscard]] bool is_deleted() const noexcept { return deleted_at.has_value(); }
    [[nodiscard]] bool is_edited() const noexcept { return edited_at.has_value(); }
};

}  // namespace rtc::models
