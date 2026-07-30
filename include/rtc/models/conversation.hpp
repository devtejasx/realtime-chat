#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// Kind of conversation. "direct" is a one-to-one chat (deduplicated per user
// pair); "group" is a named, owned, multi-member chat.
enum class ConversationType {
    kDirect,
    kGroup,
};

[[nodiscard]] constexpr std::string_view to_string(ConversationType type) noexcept {
    switch (type) {
        case ConversationType::kDirect:
            return "direct";
        case ConversationType::kGroup:
            return "group";
    }
    return "direct";
}

[[nodiscard]] constexpr std::optional<ConversationType> conversation_type_from_string(
    std::string_view value) noexcept {
    if (value == "direct")
        return ConversationType::kDirect;
    if (value == "group")
        return ConversationType::kGroup;
    return std::nullopt;
}

// Persistent conversation entity (row in `conversations`). Pure data.
struct Conversation {
    std::int64_t id = 0;
    ConversationType type = ConversationType::kDirect;
    std::optional<std::string> name;        // group name; unset for direct
    std::optional<std::int64_t> owner_id;   // group owner; unset for direct
    std::optional<std::string> direct_key;  // canonical pair key for direct
    utils::TimePoint created_at{};
    utils::TimePoint updated_at{};
    std::optional<utils::TimePoint> last_message_at;

    [[nodiscard]] bool is_group() const noexcept { return type == ConversationType::kGroup; }
    [[nodiscard]] bool is_direct() const noexcept { return type == ConversationType::kDirect; }
};

}  // namespace rtc::models
