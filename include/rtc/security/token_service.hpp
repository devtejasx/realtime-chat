#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "rtc/security/token.hpp"

namespace rtc::security {

// Abstraction over token issuance and verification. Controllers/middlewares
// depend on this interface rather than a concrete JWT library, so the signing
// scheme is swappable and easily faked in tests.
class ITokenService {
  public:
    virtual ~ITokenService() = default;

    // Issues an access + refresh pair for the given identity.
    [[nodiscard]] virtual TokenPair issue_pair(std::int64_t user_id,
                                               std::string_view username) const = 0;

    // Issues a single token of the given type.
    [[nodiscard]] virtual std::string issue(std::int64_t user_id,
                                            std::string_view username,
                                            TokenType type) const = 0;

    // Verifies signature, issuer, expiry and type of `token`, returning its
    // claims. Throws rtc::errors::AuthenticationException when the token is
    // invalid, expired, or of the wrong type.
    [[nodiscard]] virtual TokenClaims verify(std::string_view token,
                                             TokenType expected_type) const = 0;
};

}  // namespace rtc::security
