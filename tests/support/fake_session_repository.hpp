#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/models/session.hpp"
#include "rtc/repositories/session_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory ISessionRepository for service/controller tests.
class FakeSessionRepository final : public repositories::ISessionRepository {
public:
    models::Session create(const repositories::NewSession& input) override {
        models::Session s;
        s.id = input.id;
        s.user_id = input.user_id;
        s.refresh_token_hash = input.refresh_token_hash;
        s.user_agent = input.user_agent;
        s.ip = input.ip;
        s.created_at = utils::now();
        s.last_used_at = s.created_at;
        s.expires_at = s.created_at + std::chrono::seconds(input.ttl_seconds);
        sessions_.push_back(s);
        return s;
    }

    std::optional<models::Session> find_by_id(std::string_view id) override {
        for (const auto& s : sessions_)
            if (s.id == id) return s;
        return std::nullopt;
    }

    std::vector<models::Session> list_active_for_user(std::int64_t user_id) override {
        std::vector<models::Session> out;
        for (const auto& s : sessions_)
            if (s.user_id == user_id && s.is_active()) out.push_back(s);
        return out;
    }

    void rotate(std::string_view id, std::string_view new_hash) override {
        for (auto& s : sessions_)
            if (s.id == id) {
                s.refresh_token_hash = std::string(new_hash);
                s.last_used_at = utils::now();
            }
    }

    bool revoke(std::string_view id, std::int64_t user_id) override {
        for (auto& s : sessions_)
            if (s.id == id && s.user_id == user_id && !s.is_revoked()) {
                s.revoked_at = utils::now();
                return true;
            }
        return false;
    }

    std::int64_t revoke_all(std::int64_t user_id) override {
        std::int64_t count = 0;
        for (auto& s : sessions_)
            if (s.user_id == user_id && !s.is_revoked()) {
                s.revoked_at = utils::now();
                ++count;
            }
        return count;
    }

    std::int64_t revoke_all_except(std::int64_t user_id, std::string_view keep_id) override {
        std::int64_t count = 0;
        for (auto& s : sessions_)
            if (s.user_id == user_id && s.id != keep_id && !s.is_revoked()) {
                s.revoked_at = utils::now();
                ++count;
            }
        return count;
    }

    std::int64_t delete_expired() override {
        const auto before = sessions_.size();
        sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                       [](const auto& s) { return s.is_expired() || s.is_revoked(); }),
                        sessions_.end());
        return static_cast<std::int64_t>(before - sessions_.size());
    }

    [[nodiscard]] std::size_t count() const noexcept { return sessions_.size(); }

private:
    std::vector<models::Session> sessions_;
};

}  // namespace rtc::testing
