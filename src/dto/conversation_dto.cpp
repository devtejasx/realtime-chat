#include "rtc/dto/conversation_dto.hpp"

#include <string>

#include "rtc/dto/user_dto.hpp"  // opt_to_json
#include "rtc/errors/exceptions.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::dto {
namespace {

using rtc::errors::ValidationException;

[[nodiscard]] std::string require_string(const nlohmann::json& body, const char* field) {
    const auto it = body.find(field);
    if (it == body.end() || it->is_null()) {
        throw ValidationException(std::string("Missing required field: ") + field,
                                  std::string("field=") + field);
    }
    if (!it->is_string()) {
        throw ValidationException(std::string("Field must be a string: ") + field,
                                  std::string("field=") + field);
    }
    return it->get<std::string>();
}

[[nodiscard]] std::optional<std::string> to_opt_iso(const std::optional<utils::TimePoint>& tp) {
    if (!tp)
        return std::nullopt;
    return utils::to_iso8601(*tp);
}

}  // namespace

CreateConversationRequest CreateConversationRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    CreateConversationRequest request;

    const std::string type_str = require_string(body, "type");
    const auto type = models::conversation_type_from_string(type_str);
    if (!type) {
        throw ValidationException("type must be 'direct' or 'group'", "field=type");
    }
    request.type = *type;

    if (const auto it = body.find("name"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) {
            throw ValidationException("name must be a string", "field=name");
        }
        request.name = it->get<std::string>();
    }

    const auto ids = body.find("participant_ids");
    if (ids == body.end() || !ids->is_array()) {
        throw ValidationException("participant_ids must be an array of user ids",
                                  "field=participant_ids");
    }
    for (const auto& entry : *ids) {
        if (!entry.is_number_integer()) {
            throw ValidationException("participant_ids must contain integers",
                                      "field=participant_ids");
        }
        request.participant_ids.push_back(entry.get<std::int64_t>());
    }
    return request;
}

RenameGroupRequest RenameGroupRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    RenameGroupRequest request;
    request.name = require_string(body, "name");
    return request;
}

AddMemberRequest AddMemberRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    const auto it = body.find("user_id");
    if (it == body.end() || !it->is_number_integer()) {
        throw ValidationException("user_id must be an integer", "field=user_id");
    }
    return AddMemberRequest{it->get<std::int64_t>()};
}

ParticipantResponse ParticipantResponse::from(const models::ConversationParticipant& p) {
    return ParticipantResponse{
        .user_id = p.user_id,
        .role = std::string(models::to_string(p.role)),
        .joined_at = utils::to_iso8601(p.joined_at),
    };
}

nlohmann::json ParticipantResponse::to_json() const {
    return nlohmann::json{{"user_id", user_id}, {"role", role}, {"joined_at", joined_at}};
}

ConversationResponse ConversationResponse::from(
    const models::Conversation& conversation,
    const std::vector<models::ConversationParticipant>& participants) {
    ConversationResponse response;
    response.id = conversation.id;
    response.type = std::string(models::to_string(conversation.type));
    response.name = conversation.name;
    response.owner_id = conversation.owner_id;
    response.created_at = utils::to_iso8601(conversation.created_at);
    response.updated_at = utils::to_iso8601(conversation.updated_at);
    response.last_message_at = to_opt_iso(conversation.last_message_at);
    response.participants.reserve(participants.size());
    for (const auto& p : participants) {
        response.participants.push_back(ParticipantResponse::from(p));
    }
    return response;
}

nlohmann::json ConversationResponse::to_json() const {
    nlohmann::json participants_json = nlohmann::json::array();
    for (const auto& p : participants) {
        participants_json.push_back(p.to_json());
    }
    return nlohmann::json{
        {"id", id},
        {"type", type},
        {"name", opt_to_json(name)},
        {"owner_id", owner_id ? nlohmann::json(*owner_id) : nlohmann::json(nullptr)},
        {"created_at", created_at},
        {"updated_at", updated_at},
        {"last_message_at", opt_to_json(last_message_at)},
        {"participants", std::move(participants_json)},
    };
}

}  // namespace rtc::dto
