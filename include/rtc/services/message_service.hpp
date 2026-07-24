#pragma once

#include <cstdint>
#include <vector>

#include "rtc/dto/message_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/models/message.hpp"
#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/repositories/message_repository.hpp"

namespace rtc::services {

// Business logic for messages. Guarantees the required ordering — persist
// first, broadcast second — and enforces membership/authorship rules. The same
// methods back both the REST controllers and the WebSocket handlers.
class MessageService {
public:
    MessageService(repositories::IMessageRepository& messages,
                   repositories::IConversationRepository& conversations,
                   realtime::IEventBroadcaster& broadcaster) noexcept
        : messages_(messages), conversations_(conversations), broadcaster_(broadcaster) {}

    // Validates, stores, then broadcasts a new message. The sender must be a
    // participant of the target conversation.
    [[nodiscard]] models::Message send(std::int64_t actor_id,
                                       const dto::SendMessageRequest& request);

    // Lists/searches messages in a conversation the actor participates in.
    [[nodiscard]] std::vector<models::Message> list(std::int64_t actor_id,
                                                    const dto::MessageQuery& query,
                                                    const dto::Pagination& page);

    // Edits a message; only its author may edit. Broadcasts the update.
    [[nodiscard]] models::Message edit(std::int64_t actor_id, std::int64_t message_id,
                                       const dto::UpdateMessageRequest& request);

    // Soft-deletes a message; the author or the group owner may delete.
    [[nodiscard]] models::Message remove(std::int64_t actor_id, std::int64_t message_id);

    // Fetches a single message the actor may see.
    [[nodiscard]] models::Message get(std::int64_t actor_id, std::int64_t message_id);

private:
    [[nodiscard]] models::Message require_message(std::int64_t message_id);
    void require_participant(std::int64_t conversation_id, std::int64_t user_id);

    repositories::IMessageRepository& messages_;
    repositories::IConversationRepository& conversations_;
    realtime::IEventBroadcaster& broadcaster_;
};

}  // namespace rtc::services
