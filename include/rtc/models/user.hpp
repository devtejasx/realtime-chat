#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// Persistent user entity — the in-memory representation of a row in the
// `users` table. This is a pure data holder with no behaviour; business rules
// live in the service layer and persistence in the repository layer.
//
// `password_hash` holds the bcrypt hash and must never be serialised into an
// API response (see rtc::dto::UserResponse for the public projection).
//
// Phase 2 adds optional public profile fields (display_name, bio, avatar_url).
// They are std::optional to distinguish "unset" (NULL) from an empty string.
struct User {
    std::int64_t id = 0;
    std::string username;
    std::string email;
    std::string password_hash;
    std::optional<std::string> display_name;
    std::optional<std::string> bio;
    std::optional<std::string> avatar_url;
    utils::TimePoint created_at{};
    utils::TimePoint updated_at{};
};

}  // namespace rtc::models
