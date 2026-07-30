#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "rtc/notifications/notification_dispatcher.hpp"
#include "rtc/services/notification_service.hpp"

namespace rtc::services {

// Concrete event-driven notification dispatcher. Translates domain events into
// persisted, delivered notifications via NotificationService, fanning out to
// the right recipients. Each handler swallows and logs its own errors to honour
// the noexcept contract, so a notification failure never affects the operation
// that triggered it.
class NotificationDispatcher final : public notifications::INotificationDispatcher {
  public:
    explicit NotificationDispatcher(NotificationService& notifications) noexcept
        : notifications_(notifications) {}

    void new_message(std::int64_t sender_id,
                     std::int64_t conversation_id,
                     std::int64_t message_id,
                     const std::vector<std::int64_t>& recipient_ids) noexcept override;
    void added_to_group(std::int64_t conversation_id,
                        std::int64_t added_user_id,
                        std::int64_t actor_id) noexcept override;
    void removed_from_group(std::int64_t conversation_id,
                            std::int64_t removed_user_id,
                            std::int64_t actor_id) noexcept override;
    void reaction_added(std::int64_t message_author_id,
                        std::int64_t message_id,
                        std::int64_t reactor_id,
                        std::string_view emoji) noexcept override;
    void profile_updated(std::int64_t user_id) noexcept override;

  private:
    NotificationService& notifications_;
};

}  // namespace rtc::services
