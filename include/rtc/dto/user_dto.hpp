#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rtc/models/user.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::dto {

// Serialises an optional string as a JSON string or null.
[[nodiscard]] inline nlohmann::json opt_to_json(const std::optional<std::string>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

// Public projection of a User. Deliberately omits `password_hash` so secret
// material can never leak through the API. Built from a model via `from`.
//
// Two serialisations are provided: to_json() is the full self-view (includes
// email) used by auth and GET /api/users/me; to_public_json() is the view of
// another user (GET /api/users/{id}) and omits the private email address.
struct UserResponse {
    std::int64_t id = 0;
    std::string username;
    std::string email;
    std::optional<std::string> display_name;
    std::optional<std::string> bio;
    std::optional<std::string> avatar_url;
    std::string created_at;  // ISO-8601
    std::string updated_at;  // ISO-8601

    [[nodiscard]] static UserResponse from(const models::User& user) {
        return UserResponse{
            .id = user.id,
            .username = user.username,
            .email = user.email,
            .display_name = user.display_name,
            .bio = user.bio,
            .avatar_url = user.avatar_url,
            .created_at = utils::to_iso8601(user.created_at),
            .updated_at = utils::to_iso8601(user.updated_at),
        };
    }

    [[nodiscard]] nlohmann::json to_json() const {
        auto json = to_public_json();
        json["email"] = email;
        return json;
    }

    [[nodiscard]] nlohmann::json to_public_json() const {
        return nlohmann::json{
            {"id", id},
            {"username", username},
            {"display_name", opt_to_json(display_name)},
            {"bio", opt_to_json(bio)},
            {"avatar_url", opt_to_json(avatar_url)},
            {"created_at", created_at},
            {"updated_at", updated_at},
        };
    }
};

}  // namespace rtc::dto
