#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/models/notification.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::dto {

// Outbound representation of a notification.
struct NotificationResponse {
    std::int64_t id = 0;
    std::string type;
    nlohmann::json payload;
    bool read = false;
    std::string created_at;

    [[nodiscard]] static NotificationResponse from(const models::Notification& n) {
        return NotificationResponse{
            .id = n.id,
            .type = std::string(models::to_string(n.type)),
            .payload = n.payload,
            .read = n.is_read(),
            .created_at = utils::to_iso8601(n.created_at),
        };
    }

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{
            {"id", id},
            {"type", type},
            {"payload", payload},
            {"read", read},
            {"created_at", created_at},
        };
    }
};

}  // namespace rtc::dto
