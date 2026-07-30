#pragma once

#include <optional>
#include <string>

#include <crow/http_request.h>

#include "rtc/security/token.hpp"
#include "rtc/security/token_service.hpp"

namespace rtc::services {
class AuthorizationService;  // forward-declared: keeps the service layer out of this header
}  // namespace rtc::services

namespace rtc::middlewares {

// JWT authentication guard for protected routes.
//
// Not a global Crow middleware (which cannot easily receive injected
// dependencies); instead an explicit, DI-friendly guard that a controller
// invokes at the top of any handler requiring authentication. It extracts the
// Bearer token from the Authorization header, verifies it as an *access*
// token, and returns the caller's claims — or throws AuthenticationException
// (HTTP 401) which the controller translates via ErrorMapper.
//
// Account suspension
// ------------------
// An AuthorizationService may be attached after construction. When present,
// authenticate() additionally rejects tokens belonging to a suspended account.
// This guard is the single choke point every protected endpoint already passes
// through, which makes it the correct place for that check: a ban then takes
// effect on the caller's *next request* rather than whenever their access token
// happens to expire. The lookup is cache-backed, so the cost is one cache hit.
//
// The dependency is optional and injected via a setter rather than the
// constructor, so every existing caller — including the unit tests that
// construct an AuthMiddleware with only a token service — keeps compiling and
// behaving exactly as before.
class AuthMiddleware {
public:
    explicit AuthMiddleware(const security::ITokenService& token_service) noexcept
        : token_service_(token_service) {}

    // Enables account-suspension enforcement. Call once at startup.
    void set_authorization_service(services::AuthorizationService& authorization) noexcept {
        authorization_ = &authorization;
    }

    // Authenticates the request, returning verified access-token claims.
    // Throws rtc::errors::AuthenticationException when the header is missing,
    // malformed, the token is invalid/expired, or the account is suspended.
    [[nodiscard]] security::TokenClaims authenticate(const crow::request& request) const;

    // Verifies the token but skips the suspension check. Needed by the few
    // endpoints that must remain reachable while suspended — logout, and reading
    // one's own account — so a banned user can still sign out cleanly.
    [[nodiscard]] security::TokenClaims authenticate_token_only(
        const crow::request& request) const;

    // Extracts the raw token from an "Authorization: Bearer <token>" header.
    // Returns nullopt when absent or not a well-formed Bearer header.
    [[nodiscard]] static std::optional<std::string> extract_bearer_token(
        const crow::request& request);

private:
    const security::ITokenService& token_service_;
    services::AuthorizationService* authorization_ = nullptr;
};

}  // namespace rtc::middlewares
