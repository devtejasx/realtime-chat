#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "rtc/security/token_service.hpp"

namespace rtc::security {

// HMAC-SHA256 (HS256) JWT implementation of ITokenService, backed by jwt-cpp.
//
// Tokens carry the standard iss/iat/exp/sub claims plus a custom "username"
// and "type" claim. Verification enforces the signature, issuer, expiry and
// the expected token type. The signing secret is provided by configuration and
// never logged.
class JwtTokenService final : public ITokenService {
public:
    struct Options {
        std::string secret;
        std::string issuer = "realtime-chat";
        std::int64_t access_ttl_seconds = 900;
        std::int64_t refresh_ttl_seconds = 1'209'600;
    };

    explicit JwtTokenService(Options options);

    [[nodiscard]] TokenPair issue_pair(std::int64_t user_id,
                                       std::string_view username) const override;

    [[nodiscard]] std::string issue(std::int64_t user_id, std::string_view username,
                                     TokenType type) const override;

    [[nodiscard]] TokenClaims verify(std::string_view token,
                                     TokenType expected_type) const override;

private:
    [[nodiscard]] std::int64_t ttl_for(TokenType type) const noexcept;

    Options options_;
};

}  // namespace rtc::security
