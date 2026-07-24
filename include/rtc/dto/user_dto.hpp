#pragma once

#include <nlohmann/json.hpp>

#include "rtc/models/user.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::dto {

// Public projection of a User. Deliberately omits `password_hash` so secret
// material can never leak through the API. Built from a model via `from`.
struct UserResponse {
    std::int64_t id = 0;
    std::string username;
    std::string email;
    std::string created_at;  // ISO-8601
    std::string updated_at;  // ISO-8601

    [[nodiscard]] static UserResponse from(const models::User& user) {
        return UserResponse{
            .id = user.id,
            .username = user.username,
            .email = user.email,
            .created_at = utils::to_iso8601(user.created_at),
            .updated_at = utils::to_iso8601(user.updated_at),
        };
    }

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{
            {"id", id},
            {"username", username},
            {"email", email},
            {"created_at", created_at},
            {"updated_at", updated_at},
        };
    }
};

}  // namespace rtc::dto
