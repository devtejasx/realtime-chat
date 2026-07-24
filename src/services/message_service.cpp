#include "rtc/services/message_service.hpp"

#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/realtime/events.hpp"
#include "rtc/validation/validators.hpp"

namespace rtc::services {

namespace {
using rtc::errors::AuthorizationException;
using rtc::errors::NotFoundException;
}  // namespace

models::Message MessageService::require_message(std::int64_t message_id) {
    auto message = messages_.find_by_id(message_id);
    if (!message) {
        throw NotFoundException("Message not found",
                                "message_id=" + std::to_string(message_id));
    }
    return *message;
}

void MessageService::require_participant(std::int64_t conversation_id, std::int64_t user_id) {
    if (!conversations_.is_participant(conversation_id, user_id)) {
        throw NotFoundException("Conversation not found");
    }
}

models::Message MessageService::send(std::int64_t actor_id,
                                     const dto::SendMessageRequest& request) {
    const std::string content = validation::validate_message_content(request.content);
    require_participant(request.conversation_id, actor_id);

    // 1) Persist first — durability before delivery.
    repositories::NewMessage input;
    input.conversation_id = request.conversation_id;
    input.sender_id = actor_id;
    input.content = content;
    input.type = request.type;
    const models::Message stored = messages_.create(input);

    // 2) Broadcast second — to every participant (including the sender's other
    // sessions) so clients converge on the persisted state.
    broadcaster_.publish(conversations_.list_participant_ids(request.conversation_id),
                         realtime::events::kMessageCreated,
                         dto::MessageResponse::from(stored).to_json());
    return stored;
}

std::vector<models::Message> MessageService::list(std::int64_t actor_id,
                                                  const dto::MessageQuery& query,
                                                  const dto::Pagination& page) {
    require_participant(query.conversation_id, actor_id);
    repositories::MessageFilter filter;
    filter.conversation_id = query.conversation_id;
    filter.sender_id = query.sender_id;
    filter.keyword = query.keyword;
    return messages_.list(filter, page);
}

models::Message MessageService::edit(std::int64_t actor_id, std::int64_t message_id,
                                     const dto::UpdateMessageRequest& request) {
    const auto existing = require_message(message_id);
    if (existing.sender_id != actor_id) {
        throw AuthorizationException("Only the author may edit a message");
    }
    const std::string content = validation::validate_message_content(request.content);
    const models::Message updated = messages_.update_content(message_id, content);

    broadcaster_.publish(conversations_.list_participant_ids(updated.conversation_id),
                         realtime::events::kMessageUpdated,
                         dto::MessageResponse::from(updated).to_json());
    return updated;
}

models::Message MessageService::remove(std::int64_t actor_id, std::int64_t message_id) {
    const auto existing = require_message(message_id);

    bool permitted = existing.sender_id == actor_id;
    if (!permitted) {
        // A group owner may remove any message in their group.
        const auto conversation = conversations_.find_by_id(existing.conversation_id);
        permitted = conversation && conversation->is_group() &&
                    conversation->owner_id == actor_id;
    }
    if (!permitted) {
        throw AuthorizationException("Not allowed to delete this message");
    }

    const models::Message deleted = messages_.soft_delete(message_id);
    broadcaster_.publish(conversations_.list_participant_ids(deleted.conversation_id),
                         realtime::events::kMessageDeleted,
                         dto::MessageResponse::from(deleted).to_json());
    return deleted;
}

models::Message MessageService::get(std::int64_t actor_id, std::int64_t message_id) {
    const auto message = require_message(message_id);
    require_participant(message.conversation_id, actor_id);
    return message;
}

}  // namespace rtc::services
