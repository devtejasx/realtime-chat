#include "rtc/services/conversation_service.hpp"

#include <algorithm>
#include <set>
#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/realtime/events.hpp"
#include "rtc/validation/validators.hpp"

namespace rtc::services {

namespace {
using rtc::errors::AuthorizationException;
using rtc::errors::NotFoundException;
using rtc::errors::ValidationException;
}  // namespace

models::Conversation ConversationService::require_conversation(std::int64_t conversation_id) {
    auto conversation = conversations_.find_by_id(conversation_id);
    if (!conversation) {
        throw NotFoundException("Conversation not found",
                                "conversation_id=" + std::to_string(conversation_id));
    }
    return *conversation;
}

void ConversationService::require_participant(std::int64_t conversation_id,
                                              std::int64_t user_id) {
    if (!conversations_.is_participant(conversation_id, user_id)) {
        // Hide existence from non-members.
        throw NotFoundException("Conversation not found");
    }
}

void ConversationService::require_owner(const models::Conversation& conversation,
                                        std::int64_t user_id) {
    if (!conversation.is_group()) {
        throw ValidationException("Operation is only valid for group conversations");
    }
    if (conversation.owner_id != user_id) {
        throw AuthorizationException("Only the group owner may perform this action");
    }
}

void ConversationService::require_user_exists(std::int64_t user_id) {
    if (!users_.find_by_id(user_id)) {
        throw NotFoundException("User not found", "user_id=" + std::to_string(user_id));
    }
}

events::IEventPublisher& ConversationService::publisher() const noexcept {
    return publisher_ != nullptr ? *publisher_ : events::NullEventPublisher::instance();
}

models::Conversation ConversationService::create(std::int64_t actor_id,
                                                 const dto::CreateConversationRequest& request) {
    models::Conversation conversation;

    if (request.type == models::ConversationType::kDirect) {
        if (request.participant_ids.size() != 1) {
            throw ValidationException("A direct conversation requires exactly one other user",
                                      "field=participant_ids");
        }
        const std::int64_t other = request.participant_ids.front();
        if (other == actor_id) {
            throw ValidationException("Cannot start a direct conversation with yourself",
                                      "field=participant_ids");
        }
        require_user_exists(other);
        conversation = conversations_.create_or_get_direct(actor_id, other);
    } else {
        if (!request.name) {
            throw ValidationException("A group requires a name", "field=name");
        }
        const std::string name = validation::validate_group_name(*request.name);

        // Deduplicate members and drop the actor (added as owner automatically).
        std::set<std::int64_t> unique(request.participant_ids.begin(),
                                      request.participant_ids.end());
        unique.erase(actor_id);
        std::vector<std::int64_t> members(unique.begin(), unique.end());
        for (const std::int64_t member : members) {
            require_user_exists(member);
        }
        conversation = conversations_.create_group(actor_id, name, members);
    }

    const auto participants = conversations_.list_participants(conversation.id);
    const auto ids = conversations_.list_participant_ids(conversation.id);
    broadcaster_.publish(ids, realtime::events::kConversationCreated,
                         dto::ConversationResponse::from(conversation, participants).to_json());
    publisher().publish(events::ConversationCreated{
        .conversation_id = conversation.id,
        .actor_id = actor_id,
        .is_group = conversation.is_group(),
        .participant_ids = ids,
    }.to_event());
    return conversation;
}

models::Conversation ConversationService::get(std::int64_t actor_id,
                                              std::int64_t conversation_id) {
    auto conversation = require_conversation(conversation_id);
    require_participant(conversation_id, actor_id);
    return conversation;
}

std::vector<models::Conversation> ConversationService::list(std::int64_t actor_id,
                                                            const dto::Pagination& page) {
    return conversations_.list_for_user(actor_id, page);
}

std::vector<models::ConversationParticipant> ConversationService::participants(
    std::int64_t conversation_id) {
    return conversations_.list_participants(conversation_id);
}

std::vector<std::int64_t> ConversationService::participant_ids(std::int64_t conversation_id) {
    return conversations_.list_participant_ids(conversation_id);
}

std::vector<std::int64_t> ConversationService::conversation_ids(std::int64_t user_id) {
    return conversations_.list_conversation_ids(user_id);
}

std::vector<std::int64_t> ConversationService::peer_ids(std::int64_t user_id) {
    return conversations_.list_peer_ids(user_id);
}

void ConversationService::remove(std::int64_t actor_id, std::int64_t conversation_id) {
    const auto conversation = require_conversation(conversation_id);
    if (conversation.is_group()) {
        require_owner(conversation, actor_id);
    } else {
        require_participant(conversation_id, actor_id);
    }
    const auto ids = conversations_.list_participant_ids(conversation_id);
    conversations_.remove(conversation_id);
    broadcaster_.publish(ids, realtime::events::kConversationDeleted,
                         nlohmann::json{{"conversation_id", conversation_id}});
    publisher().publish(events::ConversationDeleted{
        .conversation_id = conversation_id,
        .actor_id = actor_id,
    }.to_event());
}

models::Conversation ConversationService::rename_group(
    std::int64_t actor_id, std::int64_t conversation_id,
    const dto::RenameGroupRequest& request) {
    const auto conversation = require_conversation(conversation_id);
    require_owner(conversation, actor_id);
    const std::string name = validation::validate_group_name(request.name);
    conversations_.rename(conversation_id, name);
    auto updated = require_conversation(conversation_id);
    broadcaster_.publish(conversations_.list_participant_ids(conversation_id),
                         realtime::events::kConversationCreated,
                         dto::ConversationResponse::from(
                             updated, conversations_.list_participants(conversation_id))
                             .to_json());
    return updated;
}

models::ConversationParticipant ConversationService::add_member(std::int64_t actor_id,
                                                                std::int64_t conversation_id,
                                                                std::int64_t user_id) {
    const auto conversation = require_conversation(conversation_id);
    require_owner(conversation, actor_id);
    require_user_exists(user_id);
    conversations_.add_participant(conversation_id, user_id, models::ParticipantRole::kMember);

    auto participant = conversations_.find_participant(conversation_id, user_id);
    if (!participant) {
        throw errors::InternalException("Failed to load newly added participant");
    }
    broadcaster_.publish(
        conversations_.list_participant_ids(conversation_id), realtime::events::kMemberAdded,
        nlohmann::json{{"conversation_id", conversation_id}, {"user_id", user_id}});
    notifications_.added_to_group(conversation_id, user_id, actor_id);
    publisher().publish(events::MemberAdded{
        .conversation_id = conversation_id,
        .member_id = user_id,
        .actor_id = actor_id,
    }.to_event());
    return *participant;
}

void ConversationService::remove_member(std::int64_t actor_id, std::int64_t conversation_id,
                                        std::int64_t target_user_id) {
    const auto conversation = require_conversation(conversation_id);
    require_owner(conversation, actor_id);
    if (conversation.owner_id == target_user_id) {
        throw ValidationException("The owner cannot be removed; transfer ownership or delete");
    }
    const auto ids = conversations_.list_participant_ids(conversation_id);
    conversations_.remove_participant(conversation_id, target_user_id);
    broadcaster_.publish(
        ids, realtime::events::kMemberRemoved,
        nlohmann::json{{"conversation_id", conversation_id}, {"user_id", target_user_id}});
    notifications_.removed_from_group(conversation_id, target_user_id, actor_id);
    publisher().publish(events::MemberRemoved{
        .conversation_id = conversation_id,
        .member_id = target_user_id,
        .actor_id = actor_id,
    }.to_event());
}

void ConversationService::leave(std::int64_t actor_id, std::int64_t conversation_id) {
    const auto conversation = require_conversation(conversation_id);
    require_participant(conversation_id, actor_id);

    const auto ids = conversations_.list_participant_ids(conversation_id);

    if (conversation.is_group() && conversation.owner_id == actor_id) {
        // Owner is leaving: hand off to the earliest-joined remaining member,
        // or delete the group if the owner was the last member.
        const auto members = conversations_.list_participants(conversation_id);
        auto successor = std::find_if(members.begin(), members.end(),
                                      [&](const auto& p) { return p.user_id != actor_id; });
        if (successor != members.end()) {
            conversations_.transfer_ownership(conversation_id, successor->user_id);
            conversations_.remove_participant(conversation_id, actor_id);
        } else {
            conversations_.remove(conversation_id);
        }
    } else {
        conversations_.remove_participant(conversation_id, actor_id);
    }

    broadcaster_.publish(
        ids, realtime::events::kMemberRemoved,
        nlohmann::json{{"conversation_id", conversation_id}, {"user_id", actor_id}});
}

}  // namespace rtc::services
