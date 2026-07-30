#pragma once

#include <crow/http_request.h>

#include <nlohmann/json.hpp>

#include "rtc/errors/exceptions.hpp"

namespace rtc::http {

// Parses a request body as JSON, translating malformed input into a 400
// ValidationException rather than letting nlohmann throw an opaque error.
// Shared by every REST controller so body parsing behaves identically.
[[nodiscard]] inline nlohmann::json parse_json_body(const crow::request& req) {
    nlohmann::json parsed =
        nlohmann::json::parse(req.body, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        throw errors::ValidationException("Request body is not valid JSON");
    }
    return parsed;
}

}  // namespace rtc::http
