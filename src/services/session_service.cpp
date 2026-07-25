#include "rtc/services/session_service.hpp"

#include <utility>

#include "rtc/errors/exceptions.hpp"
#include "rtc/utils/hash.hpp"
#include "rtc/utils/random.hpp"

namespace rtc::services {

std::string SessionService::record(std::int64_t user_id, const std::string& refresh_token,
                                   std::optional<std::string> user_agent,
                                   std::optional<std::string> ip) {
    repositories::NewSession session;
    session.id = utils::generate_hex_token(24);
    session.user_id = user_id;
    session.refresh_token_hash = utils::sha256_hex(refresh_token);
    session.user_agent = std::move(user_agent);
    session.ip = std::move(ip);
    session.ttl_seconds = refresh_ttl_seconds_;
    repository_.create(session);
    return session.id;
}

security::TokenPair SessionService::rotate(const std::string& refresh_token,
                                           const std::string& session_id) {
    // Verify the JWT itself (signature, issuer, expiry, type=refresh).
    const auto claims = token_service_.verify(refresh_token, security::TokenType::kRefresh);

    const auto session = repository_.find_by_id(session_id);
    if (!session || !session->is_active()) {
        throw errors::AuthenticationException("Session is invalid or expired");
    }
    if (session->user_id != claims.user_id) {
        throw errors::AuthenticationException("Session does not match token");
    }
    // Replay protection: the presented token must match the *current* stored
    // hash. A previously-rotated token no longer matches and is rejected.
    if (session->refresh_token_hash != utils::sha256_hex(refresh_token)) {
        throw errors::AuthenticationException("Refresh token has been rotated");
    }

    security::TokenPair pair = token_service_.issue_pair(claims.user_id, claims.username);
    repository_.rotate(session_id, utils::sha256_hex(pair.refresh_token));
    return pair;
}

std::vector<models::Session> SessionService::list(std::int64_t user_id) {
    return repository_.list_active_for_user(user_id);
}

bool SessionService::revoke(std::int64_t user_id, const std::string& session_id) {
    return repository_.revoke(session_id, user_id);
}

std::int64_t SessionService::revoke_all(std::int64_t user_id) {
    return repository_.revoke_all(user_id);
}

std::int64_t SessionService::revoke_others(std::int64_t user_id,
                                           const std::string& keep_session_id) {
    return repository_.revoke_all_except(user_id, keep_session_id);
}

std::int64_t SessionService::cleanup_expired() { return repository_.delete_expired(); }

}  // namespace rtc::services
