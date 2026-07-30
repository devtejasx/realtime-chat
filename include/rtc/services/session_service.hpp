#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtc/models/session.hpp"
#include "rtc/repositories/session_repository.hpp"
#include "rtc/security/token.hpp"
#include "rtc/security/token_service.hpp"

namespace rtc::services {

// Distributed session management: records issued sessions, rotates refresh
// tokens, and revokes sessions individually or globally. The refresh token is
// stored only as a hash, and rotation replaces it on every refresh — so a
// stolen-then-rotated token is rejected on reuse (replay protection).
class SessionService {
  public:
    SessionService(repositories::ISessionRepository& repository,
                   const security::ITokenService& token_service,
                   std::int64_t refresh_ttl_seconds) noexcept
        : repository_(repository),
          token_service_(token_service),
          refresh_ttl_seconds_(refresh_ttl_seconds) {}

    // Persists a session for a just-issued refresh token; returns the session id.
    [[nodiscard]] std::string record(std::int64_t user_id,
                                     const std::string& refresh_token,
                                     std::optional<std::string> user_agent,
                                     std::optional<std::string> ip);

    // Validates and rotates: verifies the refresh token and its session, then
    // issues a fresh access/refresh pair and stores the new refresh hash.
    // Throws AuthenticationException on any mismatch.
    [[nodiscard]] security::TokenPair rotate(const std::string& refresh_token,
                                             const std::string& session_id);

    [[nodiscard]] std::vector<models::Session> list(std::int64_t user_id);

    bool revoke(std::int64_t user_id, const std::string& session_id);
    std::int64_t revoke_all(std::int64_t user_id);
    std::int64_t revoke_others(std::int64_t user_id, const std::string& keep_session_id);

    // Maintenance: purge expired/revoked sessions. Returns count removed.
    std::int64_t cleanup_expired();

  private:
    repositories::ISessionRepository& repository_;
    const security::ITokenService& token_service_;
    std::int64_t refresh_ttl_seconds_;
};

}  // namespace rtc::services
