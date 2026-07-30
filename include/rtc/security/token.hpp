#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::security {

// Which kind of JWT is being issued or validated. Encoded in the token's
// custom "type" claim so an access token can never be used where a refresh
// token is required, and vice versa.
enum class TokenType {
    kAccess,
    kRefresh,
};

[[nodiscard]] constexpr std::string_view to_string(TokenType type) noexcept {
    switch (type) {
        case TokenType::kAccess:
            return "access";
        case TokenType::kRefresh:
            return "refresh";
    }
    return "access";
}

// Decoded, validated claims extracted from a JWT.
struct TokenClaims {
    std::int64_t user_id = 0;  // "sub" claim, parsed to the numeric user id
    std::string username;      // custom "username" claim
    TokenType type = TokenType::kAccess;
    utils::TimePoint issued_at{};   // "iat"
    utils::TimePoint expires_at{};  // "exp"
};

// A freshly issued access/refresh pair returned by the auth flow.
struct TokenPair {
    std::string access_token;
    std::string refresh_token;
    std::int64_t access_expires_in_seconds = 0;  // lifetime, for the client
    std::int64_t refresh_expires_in_seconds = 0;
};

}  // namespace rtc::security
