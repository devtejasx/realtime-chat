#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace rtc::notifications {

// The event-driven seam through which domain services announce noteworthy
// events. Producers (message, conversation, reaction, profile services) depend
// only on this interface and call the relevant method after they persist; they
// know nothing about how — or whether — notifications are produced. All methods
// are noexcept: the notification path must never disrupt the originating
// operation. A NullNotificationDispatcher makes the whole system work (and stay
// testable) with notifications switched off.
class INotificationDispatcher {
public:
    virtual ~INotificationDispatcher() = default;

    virtual void new_message(std::int64_t sender_id, std::int64_t conversation_id,
                             std::int64_t message_id,
                             const std::vector<std::int64_t>& recipient_ids) noexcept = 0;

    virtual void added_to_group(std::int64_t conversation_id, std::int64_t added_user_id,
                                std::int64_t actor_id) noexcept = 0;

    virtual void removed_from_group(std::int64_t conversation_id, std::int64_t removed_user_id,
                                    std::int64_t actor_id) noexcept = 0;

    virtual void reaction_added(std::int64_t message_author_id, std::int64_t message_id,
                                std::int64_t reactor_id, std::string_view emoji) noexcept = 0;

    virtual void profile_updated(std::int64_t user_id) noexcept = 0;
};

// No-op dispatcher: notifications disabled.
class NullNotificationDispatcher final : public INotificationDispatcher {
public:
    void new_message(std::int64_t, std::int64_t, std::int64_t,
                     const std::vector<std::int64_t>&) noexcept override {}
    void added_to_group(std::int64_t, std::int64_t, std::int64_t) noexcept override {}
    void removed_from_group(std::int64_t, std::int64_t, std::int64_t) noexcept override {}
    void reaction_added(std::int64_t, std::int64_t, std::int64_t,
                        std::string_view) noexcept override {}
    void profile_updated(std::int64_t) noexcept override {}
};

}  // namespace rtc::notifications
