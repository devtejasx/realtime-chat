#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// The supported reaction emoji. Reactions are restricted to this set so clients
// can rely on a fixed, renderable palette (and to bound stored values).
inline constexpr std::array<std::string_view, 7> kAllowedReactions = {
    "👍", "❤️", "😂", "😮", "😢", "👏", "🔥",
};

[[nodiscard]] inline bool is_allowed_reaction(std::string_view emoji) noexcept {
    for (const auto allowed : kAllowedReactions) {
        if (allowed == emoji) {
            return true;
        }
    }
    return false;
}

// Persistent reaction (row in `message_reactions`). One per (message, user);
// changing a reaction updates `emoji` in place.
struct Reaction {
    std::int64_t id = 0;
    std::int64_t message_id = 0;
    std::int64_t user_id = 0;
    std::string emoji;
    utils::TimePoint created_at{};
    utils::TimePoint updated_at{};
};

}  // namespace rtc::models
