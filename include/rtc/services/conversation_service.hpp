#pragma once

#include <cstdint>
#include <vector>

#include "rtc/dto/conversation_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/events/event_bus.hpp"
#include "rtc/models/conversation.hpp"
#include "rtc/models/conversation_participant.hpp"
#include "rtc/notifications/notification_dispatcher.hpp"
#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/repositories/user_repository.hpp"

namespace rtc::services {

// Business logic for conversations and group chats. Enforces membership and
// ownership rules, validates input, and emits real-time events after a change
// is persisted. Both REST controllers and WebSocket handlers call into this
// service — never the repository directly.
class ConversationService {
  public:
    ConversationService(repositories::IConversationRepository& conversations,
                        repositories::IUserRepository& users,
                        realtime::IEventBroadcaster& broadcaster,
                        notifications::INotificationDispatcher& notifications) noexcept
        : conversations_(conversations),
          users_(users),
          broadcaster_(broadcaster),
          notifications_(notifications) {}

    // Attaches the domain event bus. Optional and set after construction, so this
    // is additive for every existing caller and test (see MessageService for the
    // same pattern and rationale).
    void set_event_publisher(events::IEventPublisher& publisher) noexcept {
        publisher_ = &publisher;
    }

    // Creates a direct or group conversation on behalf of `actor_id`.
    [[nodiscard]] models::Conversation create(std::int64_t actor_id,
                                              const dto::CreateConversationRequest& request);

    // Returns a conversation the actor participates in (404 otherwise).
    [[nodiscard]] models::Conversation get(std::int64_t actor_id, std::int64_t conversation_id);

    [[nodiscard]] std::vector<models::Conversation> list(std::int64_t actor_id,
                                                         const dto::Pagination& page);

    [[nodiscard]] std::vector<models::ConversationParticipant> participants(
        std::int64_t conversation_id);

    // Deletes a conversation: direct requires participation; group requires
    // ownership.
    void remove(std::int64_t actor_id, std::int64_t conversation_id);

    // Group operations (owner-only unless noted).
    [[nodiscard]] models::Conversation rename_group(std::int64_t actor_id,
                                                    std::int64_t conversation_id,
                                                    const dto::RenameGroupRequest& request);
    [[nodiscard]] models::ConversationParticipant add_member(std::int64_t actor_id,
                                                             std::int64_t conversation_id,
                                                             std::int64_t user_id);
    void remove_member(std::int64_t actor_id,
                       std::int64_t conversation_id,
                       std::int64_t target_user_id);
    // Any participant may leave; when the owner leaves, ownership transfers to
    // the earliest-joined member, or the group is deleted if none remain.
    void leave(std::int64_t actor_id, std::int64_t conversation_id);

    // Convenience for callers needing the fan-out target set.
    [[nodiscard]] std::vector<std::int64_t> participant_ids(std::int64_t conversation_id);

    // All conversation ids the user belongs to (realtime room subscription).
    [[nodiscard]] std::vector<std::int64_t> conversation_ids(std::int64_t user_id);

    // Users sharing a conversation with `user_id` (presence audience).
    [[nodiscard]] std::vector<std::int64_t> peer_ids(std::int64_t user_id);

  private:
    [[nodiscard]] models::Conversation require_conversation(std::int64_t conversation_id);
    void require_participant(std::int64_t conversation_id, std::int64_t user_id);
    void require_owner(const models::Conversation& conversation, std::int64_t user_id);
    void require_user_exists(std::int64_t user_id);

    [[nodiscard]] events::IEventPublisher& publisher() const noexcept;

    repositories::IConversationRepository& conversations_;
    repositories::IUserRepository& users_;
    realtime::IEventBroadcaster& broadcaster_;
    notifications::INotificationDispatcher& notifications_;
    events::IEventPublisher* publisher_ = nullptr;
};

}  // namespace rtc::services
