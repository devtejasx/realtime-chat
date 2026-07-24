#include "rtc/dto/auth_dto.hpp"

#include <string>

#include "rtc/errors/exceptions.hpp"

namespace rtc::dto {
namespace {

using rtc::errors::ValidationException;

// Extracts a required string field, giving precise, client-safe error messages
// for the two structural failure modes (missing / wrong type).
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

}  // namespace

RegisterRequest RegisterRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    RegisterRequest request;
    request.username = require_string(body, "username");
    request.email = require_string(body, "email");
    request.password = require_string(body, "password");
    return request;
}

LoginRequest LoginRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    LoginRequest request;
    // Accept either an explicit "identifier" or, for convenience, "username"
    // or "email" as the login handle.
    if (body.contains("identifier")) {
        request.identifier = require_string(body, "identifier");
    } else if (body.contains("username")) {
        request.identifier = require_string(body, "username");
    } else if (body.contains("email")) {
        request.identifier = require_string(body, "email");
    } else {
        throw ValidationException("Missing login identifier",
                                  "field=identifier|username|email");
    }
    request.password = require_string(body, "password");
    return request;
}

}  // namespace rtc::dto
