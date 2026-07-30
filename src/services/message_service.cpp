#include "rtc/services/message_service.hpp"

#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/realtime/events.hpp"
#include "rtc/validation/validators.hpp"

namespace rtc::services {

namespace {
using rtc::errors::AuthorizationException;
using rtc::errors::NotFoundException;
}  // namespace

events::IEventPublisher& MessageService::publisher() const noexcept {
    return publisher_ != nullptr ? *publisher_ : events::NullEventPublisher::instance();
}

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

dto::MessageResponse MessageService::to_response(const models::Message& message) {
    auto response = dto::MessageResponse::from(message);
    response.attachment_ids = attachments_.attachment_ids_for(message.id);
    return response;
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

    // Link any previously-uploaded attachments (owner-scoped) to this message.
    if (!request.attachment_ids.empty()) {
        attachments_.link_to_message(actor_id, request.attachment_ids, stored.id);
    }

    const auto participants = conversations_.list_participant_ids(request.conversation_id);

    // 2) Broadcast second — to every participant (including the sender's other
    // sessions) so clients converge on the persisted state.
    broadcaster_.publish(participants, realtime::events::kMessageCreated,
                         to_response(stored).to_json());

    // 3) Notifications are event-driven and never block or fail the send.
    notifications_.new_message(actor_id, stored.conversation_id, stored.id, participants);

    // 4) Announce the domain fact. Anything that wants to react — analytics, the
    // broker, search indexing — subscribes rather than being called from here,
    // which is what keeps this method from growing a new dependency per feature.
    // Note that only the content *length* travels on the bus: message bodies stay
    // inside the domain.
    publisher().publish(events::MessageSent{
        .message_id = stored.id,
        .conversation_id = stored.conversation_id,
        .sender_id = actor_id,
        .recipient_ids = participants,
        .content_length = content.size(),
    }.to_event());
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
                         realtime::events::kMessageUpdated, to_response(updated).to_json());
    publisher().publish(events::MessageEdited{
        .message_id = updated.id,
        .conversation_id = updated.conversation_id,
        .actor_id = actor_id,
    }.to_event());
    return updated;
}

models::Message MessageService::remove(std::int64_t actor_id, std::int64_t message_id) {
    const auto existing = require_message(message_id);

    const bool is_author = existing.sender_id == actor_id;
    bool permitted = is_author;
    bool as_moderator = false;
    if (!permitted) {
        // A group owner may remove any message in their group.
        const auto conversation = conversations_.find_by_id(existing.conversation_id);
        permitted = conversation && conversation->is_group() &&
                    conversation->owner_id == actor_id;
        as_moderator = permitted;
    }
    if (!permitted) {
        throw AuthorizationException("Not allowed to delete this message");
    }

    const models::Message deleted = messages_.soft_delete(message_id);
    broadcaster_.publish(conversations_.list_participant_ids(deleted.conversation_id),
                         realtime::events::kMessageDeleted, to_response(deleted).to_json());
    // Deleting someone else's message is the case a security reviewer cares about,
    // so the distinction is recorded rather than inferred later from the ids.
    publisher().publish(events::MessageDeleted{
        .message_id = deleted.id,
        .conversation_id = deleted.conversation_id,
        .actor_id = actor_id,
        .by_moderator = as_moderator,
    }.to_event());
    return deleted;
}

models::Message MessageService::get(std::int64_t actor_id, std::int64_t message_id) {
    const auto message = require_message(message_id);
    require_participant(message.conversation_id, actor_id);
    return message;
}

}  // namespace rtc::services
