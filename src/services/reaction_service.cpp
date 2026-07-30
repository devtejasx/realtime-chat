#include "rtc/services/reaction_service.hpp"

#include <string>

#include "rtc/dto/reaction_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/realtime/events.hpp"

namespace rtc::services {

models::Message ReactionService::authorize(std::int64_t actor_id, std::int64_t message_id) {
    const auto message = messages_.find_by_id(message_id);
    if (!message) {
        throw errors::NotFoundException("Message not found");
    }
    if (!conversations_.is_participant(message->conversation_id, actor_id)) {
        throw errors::NotFoundException("Message not found");  // hide from non-members
    }
    return *message;
}

models::Reaction ReactionService::react(std::int64_t actor_id,
                                        std::int64_t message_id,
                                        std::string_view emoji) {
    if (!models::is_allowed_reaction(emoji)) {
        throw errors::ValidationException("Unsupported reaction emoji", "field=emoji");
    }
    const models::Message message = authorize(actor_id, message_id);
    const models::Reaction reaction = reactions_.upsert(message_id, actor_id, emoji);

    broadcaster_.publish(conversations_.list_participant_ids(message.conversation_id),
                         realtime::events::kReactionAdded,
                         dto::ReactionResponse::from(reaction).to_json());
    notifications_.reaction_added(message.sender_id, message_id, actor_id, emoji);
    return reaction;
}

void ReactionService::unreact(std::int64_t actor_id, std::int64_t message_id) {
    const models::Message message = authorize(actor_id, message_id);
    if (reactions_.remove(message_id, actor_id)) {
        broadcaster_.publish(conversations_.list_participant_ids(message.conversation_id),
                             realtime::events::kReactionRemoved,
                             nlohmann::json{{"message_id", message_id}, {"user_id", actor_id}});
    }
}

std::vector<models::Reaction> ReactionService::list(std::int64_t actor_id,
                                                    std::int64_t message_id) {
    authorize(actor_id, message_id);
    return reactions_.list_for_message(message_id);
}

}  // namespace rtc::services
