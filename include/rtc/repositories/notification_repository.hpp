#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/models/notification.hpp"

namespace rtc::repositories {

// Persistence boundary for notifications.
class INotificationRepository {
  public:
    virtual ~INotificationRepository() = default;

    [[nodiscard]] virtual models::Notification create(std::int64_t user_id,
                                                      models::NotificationType type,
                                                      const nlohmann::json& payload) = 0;

    [[nodiscard]] virtual std::vector<models::Notification> list_for_user(
        std::int64_t user_id, const dto::Pagination& page, bool unread_only) = 0;

    [[nodiscard]] virtual std::int64_t unread_count(std::int64_t user_id) = 0;

    virtual bool mark_read(std::int64_t id, std::int64_t user_id) = 0;
    virtual std::int64_t mark_all_read(std::int64_t user_id) = 0;
    virtual bool remove(std::int64_t id, std::int64_t user_id) = 0;
};

}  // namespace rtc::repositories
