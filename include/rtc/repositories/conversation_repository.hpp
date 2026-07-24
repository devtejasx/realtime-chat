#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/models/conversation.hpp"
#include "rtc/models/conversation_participant.hpp"

namespace rtc::repositories {

// Persistence boundary for conversations and their membership. All membership
// mutations and multi-row creations run in a single transaction inside the
// implementation, so callers never see partially-created state.
class IConversationRepository {
public:
    virtual ~IConversationRepository() = default;

    // Returns the existing direct conversation between the two users, creating
    // it (with both participants) if none exists. Idempotent and race-safe via
    // the unique direct_key.
    [[nodiscard]] virtual models::Conversation create_or_get_direct(std::int64_t user_a,
                                                                    std::int64_t user_b) = 0;

    // Creates a group owned by `owner_id`, adding the owner plus `member_ids`
    // (deduplicated) as participants. Returns the new conversation.
    [[nodiscard]] virtual models::Conversation create_group(
        std::int64_t owner_id, std::string_view name,
        const std::vector<std::int64_t>& member_ids) = 0;

    [[nodiscard]] virtual std::optional<models::Conversation> find_by_id(std::int64_t id) = 0;

    // Conversations the user participates in, most-recently-active first.
    [[nodiscard]] virtual std::vector<models::Conversation> list_for_user(
        std::int64_t user_id, const dto::Pagination& page) = 0;

    [[nodiscard]] virtual std::vector<models::ConversationParticipant> list_participants(
        std::int64_t conversation_id) = 0;

    // Participant user ids only — a cheaper query for broadcast fan-out.
    [[nodiscard]] virtual std::vector<std::int64_t> list_participant_ids(
        std::int64_t conversation_id) = 0;

    // All conversation ids the user belongs to (used to join realtime rooms).
    [[nodiscard]] virtual std::vector<std::int64_t> list_conversation_ids(
        std::int64_t user_id) = 0;

    // Distinct user ids that share at least one conversation with `user_id`
    // (the audience for that user's presence changes).
    [[nodiscard]] virtual std::vector<std::int64_t> list_peer_ids(std::int64_t user_id) = 0;

    [[nodiscard]] virtual std::optional<models::ConversationParticipant> find_participant(
        std::int64_t conversation_id, std::int64_t user_id) = 0;

    [[nodiscard]] virtual bool is_participant(std::int64_t conversation_id,
                                              std::int64_t user_id) = 0;

    // Adds a member; throws ConflictException if already present.
    virtual void add_participant(std::int64_t conversation_id, std::int64_t user_id,
                                 models::ParticipantRole role) = 0;

    virtual void remove_participant(std::int64_t conversation_id, std::int64_t user_id) = 0;

    virtual void rename(std::int64_t conversation_id, std::string_view name) = 0;

    // Transfers group ownership: sets conversations.owner_id and promotes the
    // new owner to role 'owner', demoting any previous owner to 'member'.
    virtual void transfer_ownership(std::int64_t conversation_id,
                                    std::int64_t new_owner_id) = 0;

    // Hard-deletes a conversation (participants/messages cascade).
    virtual void remove(std::int64_t conversation_id) = 0;

    // Advances the last-read high-water mark for a participant (never regresses).
    virtual void update_last_read(std::int64_t conversation_id, std::int64_t user_id,
                                  std::int64_t message_id) = 0;
};

}  // namespace rtc::repositories
