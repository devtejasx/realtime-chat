#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/models/session.hpp"

namespace rtc::repositories {

// Parameters for creating a session.
struct NewSession {
    std::string id;
    std::int64_t user_id = 0;
    std::string refresh_token_hash;
    std::optional<std::string> user_agent;
    std::optional<std::string> ip;
    std::int64_t ttl_seconds = 0;
};

// Persistence boundary for sessions (the source of truth for validation and
// revocation across instances).
class ISessionRepository {
public:
    virtual ~ISessionRepository() = default;

    [[nodiscard]] virtual models::Session create(const NewSession& input) = 0;
    [[nodiscard]] virtual std::optional<models::Session> find_by_id(std::string_view id) = 0;

    // Active (non-revoked, non-expired) sessions for a user, newest first.
    [[nodiscard]] virtual std::vector<models::Session> list_active_for_user(
        std::int64_t user_id) = 0;

    // Rotates the stored refresh-token hash and bumps last_used_at.
    virtual void rotate(std::string_view id, std::string_view new_hash) = 0;

    virtual bool revoke(std::string_view id, std::int64_t user_id) = 0;
    virtual std::int64_t revoke_all(std::int64_t user_id) = 0;
    // Revokes every session for a user *except* one (logout other devices).
    virtual std::int64_t revoke_all_except(std::int64_t user_id, std::string_view keep_id) = 0;

    // Deletes expired/revoked sessions; returns the number removed.
    virtual std::int64_t delete_expired() = 0;
};

}  // namespace rtc::repositories
