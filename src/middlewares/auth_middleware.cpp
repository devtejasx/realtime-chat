#include "rtc/middlewares/auth_middleware.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "rtc/errors/exceptions.hpp"
#include "rtc/services/authorization_service.hpp"

namespace rtc::middlewares {
namespace {

constexpr std::string_view kBearerPrefix = "Bearer ";

[[nodiscard]] std::string_view ltrim(std::string_view value) {
    const auto pos = value.find_first_not_of(" \t");
    return pos == std::string_view::npos ? std::string_view{} : value.substr(pos);
}

}  // namespace

std::optional<std::string> AuthMiddleware::extract_bearer_token(const crow::request& request) {
    const std::string& header = request.get_header_value("Authorization");
    if (header.empty()) {
        return std::nullopt;
    }
    std::string_view view = ltrim(header);
    // Compare the scheme case-insensitively per RFC 7235, then take the rest.
    if (view.size() <= kBearerPrefix.size()) {
        return std::nullopt;
    }
    const bool scheme_matches = std::equal(
        kBearerPrefix.begin(), kBearerPrefix.end() - 1, view.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    if (!scheme_matches || view[kBearerPrefix.size() - 1] != ' ') {
        return std::nullopt;
    }
    std::string_view token = ltrim(view.substr(kBearerPrefix.size()));
    if (token.empty()) {
        return std::nullopt;
    }
    return std::string(token);
}

security::TokenClaims AuthMiddleware::authenticate_token_only(const crow::request& request) const {
    const auto token = extract_bearer_token(request);
    if (!token) {
        throw rtc::errors::AuthenticationException("Missing or malformed Authorization header");
    }
    // verify() enforces signature, issuer, expiry and that this is an access
    // token; it throws AuthenticationException on any failure.
    return token_service_.verify(*token, security::TokenType::kAccess);
}

security::TokenClaims AuthMiddleware::authenticate(const crow::request& request) const {
    const security::TokenClaims claims = authenticate_token_only(request);
    if (authorization_ != nullptr) {
        // Throws AuthenticationException when the account has been suspended, so
        // a ban is effective on the next request rather than at token expiry.
        authorization_->require_active(claims.user_id);
    }
    return claims;
}

}  // namespace rtc::middlewares
