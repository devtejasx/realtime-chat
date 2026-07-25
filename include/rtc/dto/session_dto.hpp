#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "rtc/dto/user_dto.hpp"  // opt_to_json
#include "rtc/models/session.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::dto {

// Public representation of a session (never exposes the refresh-token hash).
struct SessionResponse {
    std::string id;
    std::optional<std::string> user_agent;
    std::optional<std::string> ip;
    std::string created_at;
    std::string last_used_at;
    bool current = false;

    [[nodiscard]] static SessionResponse from(const models::Session& s,
                                              const std::string& current_id) {
        return SessionResponse{
            .id = s.id,
            .user_agent = s.user_agent,
            .ip = s.ip,
            .created_at = utils::to_iso8601(s.created_at),
            .last_used_at = utils::to_iso8601(s.last_used_at),
            .current = (s.id == current_id),
        };
    }

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{
            {"id", id},
            {"user_agent", opt_to_json(user_agent)},
            {"ip", opt_to_json(ip)},
            {"created_at", created_at},
            {"last_used_at", last_used_at},
            {"current", current},
        };
    }
};

}  // namespace rtc::dto
