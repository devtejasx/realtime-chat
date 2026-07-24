#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// A member's role within a conversation. Owners (group creators) may rename,
// add/remove members, and delete the group.
enum class ParticipantRole {
    kOwner,
    kMember,
};

[[nodiscard]] constexpr std::string_view to_string(ParticipantRole role) noexcept {
    switch (role) {
        case ParticipantRole::kOwner:
            return "owner";
        case ParticipantRole::kMember:
            return "member";
    }
    return "member";
}

[[nodiscard]] constexpr std::optional<ParticipantRole> participant_role_from_string(
    std::string_view value) noexcept {
    if (value == "owner") return ParticipantRole::kOwner;
    if (value == "member") return ParticipantRole::kMember;
    return std::nullopt;
}

// Persistent membership entity (row in `conversation_participants`).
struct ConversationParticipant {
    std::int64_t id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t user_id = 0;
    ParticipantRole role = ParticipantRole::kMember;
    utils::TimePoint joined_at{};
    std::optional<std::int64_t> last_read_message_id;
};

}  // namespace rtc::models
