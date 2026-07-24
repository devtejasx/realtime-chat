#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "rtc/errors/error_type.hpp"
#include "rtc/errors/exceptions.hpp"

namespace rtc::errors {

// Builds the canonical JSON error envelope returned by every failing endpoint:
//
//   {
//     "error": {
//       "code": "validation_error",
//       "message": "Username is required",
//       "details": "field=username"      // present only when non-empty
//     }
//   }
//
// Centralising this here guarantees a single, consistent error shape across the
// whole API surface (controllers, middlewares, the global handler).
[[nodiscard]] inline nlohmann::json make_error_body(std::string_view code,
                                                    std::string_view message,
                                                    std::string_view details = {}) {
    nlohmann::json error;
    error["code"] = code;
    error["message"] = message;
    if (!details.empty()) {
        error["details"] = details;
    }
    return nlohmann::json{{"error", std::move(error)}};
}

// Convenience overload building the envelope directly from an AppException.
[[nodiscard]] inline nlohmann::json make_error_body(const AppException& ex) {
    return make_error_body(ex.code(), ex.message(), ex.details());
}

}  // namespace rtc::errors
