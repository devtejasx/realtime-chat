#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "rtc/dto/user_dto.hpp"
#include "rtc/security/token.hpp"

namespace rtc::dto {

// Inbound payload for POST /api/auth/register.
struct RegisterRequest {
    std::string username;
    std::string email;
    std::string password;

    // Parses and shape-checks a JSON body. Throws rtc::errors::ValidationException
    // when the body is not an object or a required field is missing / not a
    // string. Semantic validation (length, format) is performed separately by
    // the validation layer.
    [[nodiscard]] static RegisterRequest from_json(const nlohmann::json& body);
};

// Inbound payload for POST /api/auth/login. Login accepts either a username or
// an email in the `identifier` field, matching common UX expectations.
struct LoginRequest {
    std::string identifier;
    std::string password;

    [[nodiscard]] static LoginRequest from_json(const nlohmann::json& body);
};

// Outbound payload for successful register/login: the public user plus tokens.
struct AuthResponse {
    UserResponse user;
    security::TokenPair tokens;

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{
            {"user", user.to_json()},
            {"tokens",
             {
                 {"access_token", tokens.access_token},
                 {"refresh_token", tokens.refresh_token},
                 {"token_type", "Bearer"},
                 {"access_expires_in", tokens.access_expires_in_seconds},
                 {"refresh_expires_in", tokens.refresh_expires_in_seconds},
             }},
        };
    }
};

}  // namespace rtc::dto
