#pragma once

#include <optional>
#include <string>

#include <crow/http_request.h>

#include "rtc/security/token.hpp"
#include "rtc/security/token_service.hpp"

namespace rtc::middlewares {

// JWT authentication guard for protected routes.
//
// Not a global Crow middleware (which cannot easily receive injected
// dependencies); instead an explicit, DI-friendly guard that a controller
// invokes at the top of any handler requiring authentication. It extracts the
// Bearer token from the Authorization header, verifies it as an *access*
// token, and returns the caller's claims — or throws AuthenticationException
// (HTTP 401) which the controller translates via ErrorMapper.
class AuthMiddleware {
public:
    explicit AuthMiddleware(const security::ITokenService& token_service) noexcept
        : token_service_(token_service) {}

    // Authenticates the request, returning verified access-token claims.
    // Throws rtc::errors::AuthenticationException when the header is missing,
    // malformed, or the token is invalid/expired.
    [[nodiscard]] security::TokenClaims authenticate(const crow::request& request) const;

    // Extracts the raw token from an "Authorization: Bearer <token>" header.
    // Returns nullopt when absent or not a well-formed Bearer header.
    [[nodiscard]] static std::optional<std::string> extract_bearer_token(
        const crow::request& request);

private:
    const security::ITokenService& token_service_;
};

}  // namespace rtc::middlewares
