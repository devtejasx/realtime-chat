#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <crow/http_request.h>
#include <nlohmann/json.hpp>

#include "rtc/models/message.hpp"

namespace rtc::dto {

// Inbound payload for POST /api/messages.
struct SendMessageRequest {
    std::int64_t conversation_id = 0;
    std::string content;
    models::MessageType type = models::MessageType::kText;

    [[nodiscard]] static SendMessageRequest from_json(const nlohmann::json& body);
};

// Inbound payload for PATCH /api/messages/{id} (edit content).
struct UpdateMessageRequest {
    std::string content;
    [[nodiscard]] static UpdateMessageRequest from_json(const nlohmann::json& body);
};

// Filters for GET /api/messages. `conversation_id` is required; `sender_id` and
// `keyword` are optional and, when present, drive indexed filtering / search.
struct MessageQuery {
    std::int64_t conversation_id = 0;
    std::optional<std::int64_t> sender_id;
    std::optional<std::string> keyword;

    [[nodiscard]] static MessageQuery from_request(const crow::request& req);
};

// Outbound representation of a message. Deleted messages have their content
// redacted so clients can render a "message deleted" placeholder.
struct MessageResponse {
    std::int64_t id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t sender_id = 0;
    std::string type;
    std::string content;
    bool deleted = false;
    bool edited = false;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> edited_at;

    [[nodiscard]] static MessageResponse from(const models::Message& message);
    [[nodiscard]] nlohmann::json to_json() const;
};

}  // namespace rtc::dto
