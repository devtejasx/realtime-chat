#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "rtc/models/conversation.hpp"
#include "rtc/models/conversation_participant.hpp"

namespace rtc::dto {

// Inbound payload for POST /api/conversations.
//
// For a "direct" conversation, `participant_ids` must contain exactly the other
// user's id. For a "group", it lists the initial members (the creator is added
// automatically as owner) and `name` is required.
struct CreateConversationRequest {
    models::ConversationType type = models::ConversationType::kDirect;
    std::optional<std::string> name;
    std::vector<std::int64_t> participant_ids;

    [[nodiscard]] static CreateConversationRequest from_json(const nlohmann::json& body);
};

// Inbound payload for renaming a group (PATCH/PUT group name).
struct RenameGroupRequest {
    std::string name;
    [[nodiscard]] static RenameGroupRequest from_json(const nlohmann::json& body);
};

// Inbound payload for adding a member to a group.
struct AddMemberRequest {
    std::int64_t user_id = 0;
    [[nodiscard]] static AddMemberRequest from_json(const nlohmann::json& body);
};

// A participant as returned inside a conversation.
struct ParticipantResponse {
    std::int64_t user_id = 0;
    std::string role;
    std::string joined_at;

    [[nodiscard]] static ParticipantResponse from(const models::ConversationParticipant& p);
    [[nodiscard]] nlohmann::json to_json() const;
};

// Outbound representation of a conversation with its participants.
struct ConversationResponse {
    std::int64_t id = 0;
    std::string type;
    std::optional<std::string> name;
    std::optional<std::int64_t> owner_id;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> last_message_at;
    std::vector<ParticipantResponse> participants;

    [[nodiscard]] static ConversationResponse from(
        const models::Conversation& conversation,
        const std::vector<models::ConversationParticipant>& participants);

    [[nodiscard]] nlohmann::json to_json() const;
};

}  // namespace rtc::dto
