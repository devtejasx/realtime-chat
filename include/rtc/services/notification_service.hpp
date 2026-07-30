#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/jobs/background_executor.hpp"
#include "rtc/metrics/metrics_registry.hpp"
#include "rtc/models/notification.hpp"
#include "rtc/notifications/push_provider.hpp"
#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/repositories/notification_repository.hpp"

namespace rtc::services {

// Persists notifications, delivers them in-app over WebSocket, and dispatches
// external push asynchronously through the pluggable IPushProvider. This is the
// single sink all notification producers funnel through (via
// NotificationDispatcher), so delivery policy lives in one place.
class NotificationService {
  public:
    NotificationService(repositories::INotificationRepository& repository,
                        realtime::IEventBroadcaster& broadcaster,
                        notifications::IPushProvider& push,
                        jobs::BackgroundExecutor& executor,
                        metrics::MetricsRegistry& metrics) noexcept
        : repository_(repository),
          broadcaster_(broadcaster),
          push_(push),
          executor_(executor),
          metrics_(metrics) {}

    // Creates and delivers a notification to a single recipient.
    models::Notification create(std::int64_t user_id,
                                models::NotificationType type,
                                const nlohmann::json& payload);

    [[nodiscard]] std::vector<models::Notification> list(std::int64_t user_id,
                                                         const dto::Pagination& page,
                                                         bool unread_only);
    [[nodiscard]] std::int64_t unread_count(std::int64_t user_id);
    bool mark_read(std::int64_t id, std::int64_t user_id);
    std::int64_t mark_all_read(std::int64_t user_id);
    bool remove(std::int64_t id, std::int64_t user_id);

  private:
    repositories::INotificationRepository& repository_;
    realtime::IEventBroadcaster& broadcaster_;
    notifications::IPushProvider& push_;
    jobs::BackgroundExecutor& executor_;
    metrics::MetricsRegistry& metrics_;
};

}  // namespace rtc::services
