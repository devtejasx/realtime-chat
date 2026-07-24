#pragma once

#include <cstdint>
#include <string>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// Persistent user entity — the in-memory representation of a row in the
// `users` table. This is a pure data holder with no behaviour; business rules
// live in the service layer and persistence in the repository layer.
//
// `password_hash` holds the bcrypt hash and must never be serialised into an
// API response (see rtc::dto::UserResponse for the public projection).
struct User {
    std::int64_t id = 0;
    std::string username;
    std::string email;
    std::string password_hash;
    utils::TimePoint created_at{};
    utils::TimePoint updated_at{};
};

}  // namespace rtc::models
