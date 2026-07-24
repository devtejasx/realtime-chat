#include "rtc/security/jwt_token_service.hpp"

#include <charconv>
#include <chrono>
#include <string>
#include <utility>

#include <jwt-cpp/jwt.h>

#include "rtc/errors/exceptions.hpp"

namespace rtc::security {
namespace {

using rtc::errors::AuthenticationException;

[[nodiscard]] std::int64_t parse_subject(const std::string& subject) {
    std::int64_t value = 0;
    const char* begin = subject.data();
    const char* end = begin + subject.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        throw AuthenticationException("Malformed token subject");
    }
    return value;
}

}  // namespace

JwtTokenService::JwtTokenService(Options options) : options_(std::move(options)) {
    if (options_.secret.empty()) {
        throw rtc::errors::ConfigException("JwtTokenService requires a non-empty secret");
    }
}

std::int64_t JwtTokenService::ttl_for(TokenType type) const noexcept {
    return type == TokenType::kAccess ? options_.access_ttl_seconds
                                      : options_.refresh_ttl_seconds;
}

std::string JwtTokenService::issue(std::int64_t user_id, std::string_view username,
                                   TokenType type) const {
    const auto issued_at = std::chrono::system_clock::now();
    const auto expires_at = issued_at + std::chrono::seconds(ttl_for(type));

    return jwt::create()
        .set_type("JWT")
        .set_issuer(options_.issuer)
        .set_subject(std::to_string(user_id))
        .set_payload_claim("username", jwt::claim(std::string(username)))
        .set_payload_claim("type", jwt::claim(std::string(to_string(type))))
        .set_issued_at(issued_at)
        .set_expires_at(expires_at)
        .sign(jwt::algorithm::hs256{options_.secret});
}

TokenPair JwtTokenService::issue_pair(std::int64_t user_id, std::string_view username) const {
    TokenPair pair;
    pair.access_token = issue(user_id, username, TokenType::kAccess);
    pair.refresh_token = issue(user_id, username, TokenType::kRefresh);
    pair.access_expires_in_seconds = options_.access_ttl_seconds;
    pair.refresh_expires_in_seconds = options_.refresh_ttl_seconds;
    return pair;
}

TokenClaims JwtTokenService::verify(std::string_view token, TokenType expected_type) const {
    try {
        const auto decoded = jwt::decode(std::string(token));

        const auto verifier = jwt::verify()
                                  .allow_algorithm(jwt::algorithm::hs256{options_.secret})
                                  .with_issuer(options_.issuer);
        verifier.verify(decoded);  // throws on bad signature / issuer / expiry

        if (!decoded.has_payload_claim("type")) {
            throw AuthenticationException("Token missing type claim");
        }
        const std::string type = decoded.get_payload_claim("type").as_string();
        if (type != to_string(expected_type)) {
            throw AuthenticationException("Token has unexpected type");
        }

        TokenClaims claims;
        claims.user_id = parse_subject(decoded.get_subject());
        claims.type = expected_type;
        if (decoded.has_payload_claim("username")) {
            claims.username = decoded.get_payload_claim("username").as_string();
        }
        claims.issued_at = decoded.get_issued_at();
        claims.expires_at = decoded.get_expires_at();
        return claims;
    } catch (const AuthenticationException&) {
        throw;  // already a domain error; propagate as-is
    } catch (const std::exception& ex) {
        // jwt-cpp raises std::system_error / token_verification_exception for
        // every failure mode (expired, bad signature, malformed). Collapse them
        // into a single 401 without leaking library internals to the client.
        throw AuthenticationException("Invalid or expired token", ex.what());
    }
}

}  // namespace rtc::security
