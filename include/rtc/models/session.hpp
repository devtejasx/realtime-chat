#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// A persisted authentication session (row in `sessions`), enabling distributed
// session management: multi-device visibility, individual/global revocation,
// and refresh-token rotation. The raw refresh token is never stored — only its
// SHA-256 hash. A session is valid when it is not revoked and not expired.
struct Session {
    std::string id;  // opaque session id
    std::int64_t user_id = 0;
    std::string refresh_token_hash;
    std::optional<std::string> user_agent;
    std::optional<std::string> ip;
    utils::TimePoint created_at{};
    utils::TimePoint last_used_at{};
    utils::TimePoint expires_at{};
    std::optional<utils::TimePoint> revoked_at;

    [[nodiscard]] bool is_revoked() const noexcept { return revoked_at.has_value(); }
    [[nodiscard]] bool is_expired() const noexcept { return utils::now() >= expires_at; }
    [[nodiscard]] bool is_active() const noexcept { return !is_revoked() && !is_expired(); }
};

}  // namespace rtc::models
