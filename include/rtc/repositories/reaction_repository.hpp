#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rtc/models/reaction.hpp"

namespace rtc::repositories {

// Persistence boundary for message reactions.
class IReactionRepository {
  public:
    virtual ~IReactionRepository() = default;

    // Adds or changes the caller's reaction on a message (one per user). Returns
    // the resulting reaction.
    [[nodiscard]] virtual models::Reaction upsert(std::int64_t message_id,
                                                  std::int64_t user_id,
                                                  std::string_view emoji) = 0;

    // Removes the caller's reaction. Returns true if one was removed.
    virtual bool remove(std::int64_t message_id, std::int64_t user_id) = 0;

    [[nodiscard]] virtual std::vector<models::Reaction> list_for_message(
        std::int64_t message_id) = 0;
};

}  // namespace rtc::repositories
