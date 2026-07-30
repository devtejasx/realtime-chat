#include "rtc/dto/message_dto.hpp"

#include <charconv>
#include <string>
#include <string_view>

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

[[nodiscard]] std::int64_t require_int(const nlohmann::json& body, const char* field) {
    const auto it = body.find(field);
    if (it == body.end() || !it->is_number_integer()) {
        throw ValidationException(std::string("Field must be an integer: ") + field,
                                  std::string("field=") + field);
    }
    return it->get<std::int64_t>();
}

[[nodiscard]] std::optional<std::int64_t> query_int(const crow::request& req, const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string_view view(raw);
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (ec != std::errc{} || ptr != view.data() + view.size()) {
        throw ValidationException("Invalid integer query parameter", std::string("param=") + name);
    }
    return value;
}

}  // namespace

SendMessageRequest SendMessageRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    SendMessageRequest request;
    request.conversation_id = require_int(body, "conversation_id");
    request.content = require_string(body, "content");
    if (const auto it = body.find("type"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) {
            throw ValidationException("type must be a string", "field=type");
        }
        const auto type = models::message_type_from_string(it->get<std::string>());
        if (!type) {
            throw ValidationException("type must be 'text' or 'system'", "field=type");
        }
        request.type = *type;
    }
    if (const auto it = body.find("attachment_ids"); it != body.end() && !it->is_null()) {
        if (!it->is_array()) {
            throw ValidationException("attachment_ids must be an array of ids",
                                      "field=attachment_ids");
        }
        for (const auto& entry : *it) {
            if (!entry.is_number_integer()) {
                throw ValidationException("attachment_ids must contain integers",
                                          "field=attachment_ids");
            }
            request.attachment_ids.push_back(entry.get<std::int64_t>());
        }
    }
    return request;
}

UpdateMessageRequest UpdateMessageRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    return UpdateMessageRequest{require_string(body, "content")};
}

MessageQuery MessageQuery::from_request(const crow::request& req) {
    MessageQuery query;
    const auto conversation_id = query_int(req, "conversation_id");
    if (!conversation_id) {
        throw ValidationException("conversation_id query parameter is required",
                                  "param=conversation_id");
    }
    query.conversation_id = *conversation_id;
    query.sender_id = query_int(req, "sender_id");
    if (const char* keyword = req.url_params.get("q"); keyword != nullptr) {
        query.keyword = std::string(keyword);
    }
    return query;
}

MessageResponse MessageResponse::from(const models::Message& message) {
    MessageResponse response;
    response.id = message.id;
    response.conversation_id = message.conversation_id;
    response.sender_id = message.sender_id;
    response.type = std::string(models::to_string(message.type));
    response.deleted = message.is_deleted();
    response.edited = message.is_edited();
    response.content = message.is_deleted() ? std::string{} : message.content;
    response.created_at = utils::to_iso8601(message.created_at);
    response.updated_at = utils::to_iso8601(message.updated_at);
    if (message.edited_at) {
        response.edited_at = utils::to_iso8601(*message.edited_at);
    }
    return response;
}

nlohmann::json MessageResponse::to_json() const {
    return nlohmann::json{
        {"id", id},
        {"conversation_id", conversation_id},
        {"sender_id", sender_id},
        {"type", type},
        {"content", content},
        {"deleted", deleted},
        {"edited", edited},
        {"created_at", created_at},
        {"updated_at", updated_at},
        {"edited_at", opt_to_json(edited_at)},
        {"attachment_ids", attachment_ids},
    };
}

}  // namespace rtc::dto
