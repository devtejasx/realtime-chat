#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/notification_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed INotificationRepository.
class PgNotificationRepository final : public database::BaseRepository,
                                       public INotificationRepository {
  public:
    explicit PgNotificationRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::Notification create(std::int64_t user_id,
                                              models::NotificationType type,
                                              const nlohmann::json& payload) override;
    [[nodiscard]] std::vector<models::Notification> list_for_user(std::int64_t user_id,
                                                                  const dto::Pagination& page,
                                                                  bool unread_only) override;
    [[nodiscard]] std::int64_t unread_count(std::int64_t user_id) override;
    bool mark_read(std::int64_t id, std::int64_t user_id) override;
    std::int64_t mark_all_read(std::int64_t user_id) override;
    bool remove(std::int64_t id, std::int64_t user_id) override;
};

}  // namespace rtc::repositories
