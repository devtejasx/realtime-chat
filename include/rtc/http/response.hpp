#pragma once

#include <crow/http_response.h>
#include <nlohmann/json.hpp>

namespace rtc::http {

// Builds a Crow response carrying a JSON body and the correct content type.
// Used by every controller so success responses share the shape and headers of
// the error responses produced by ErrorMapper.
[[nodiscard]] inline crow::response json_response(int status, const nlohmann::json& body) {
    crow::response response(status, body.dump());
    response.set_header("Content-Type", "application/json");
    return response;
}

}  // namespace rtc::http
