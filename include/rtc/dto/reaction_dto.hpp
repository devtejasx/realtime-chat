#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "rtc/errors/exceptions.hpp"
#include "rtc/models/reaction.hpp"

namespace rtc::dto {

// Inbound payload for adding/changing a reaction.
struct ReactionRequest {
    std::string emoji;

    [[nodiscard]] static ReactionRequest from_json(const nlohmann::json& body) {
        if (!body.is_object()) {
            throw errors::ValidationException("Request body must be a JSON object");
        }
        const auto it = body.find("emoji");
        if (it == body.end() || !it->is_string()) {
            throw errors::ValidationException("emoji must be a string", "field=emoji");
        }
        return ReactionRequest{it->get<std::string>()};
    }
};

// Outbound representation of a reaction.
struct ReactionResponse {
    std::int64_t message_id = 0;
    std::int64_t user_id = 0;
    std::string emoji;

    [[nodiscard]] static ReactionResponse from(const models::Reaction& r) {
        return ReactionResponse{r.message_id, r.user_id, r.emoji};
    }

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{{"message_id", message_id}, {"user_id", user_id}, {"emoji", emoji}};
    }
};

}  // namespace rtc::dto
