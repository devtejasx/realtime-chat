#pragma once

#include <string_view>

namespace rtc::errors {

// High-level classification of an application error. Each value maps to a
// canonical HTTP status code and a stable machine-readable code string used in
// JSON error responses. Keeping this as an enum class (rather than raw ints)
// gives us exhaustive switch coverage and type safety at call sites.
enum class ErrorType {
    kValidation,       // malformed / invalid client input          -> 400
    kAuthentication,   // missing or invalid credentials / token    -> 401
    kAuthorization,    // authenticated but not permitted           -> 403
    kNotFound,         // requested resource does not exist         -> 404
    kConflict,         // state conflict, e.g. duplicate unique key  -> 409
    kDatabase,         // persistence-layer failure                 -> 500
    kConfiguration,    // invalid / missing configuration           -> 500
    kInternal,         // unexpected, uncategorised failure         -> 500
};

// Canonical HTTP status code for an error type.
[[nodiscard]] constexpr int http_status_for(ErrorType type) noexcept {
    switch (type) {
        case ErrorType::kValidation:
            return 400;
        case ErrorType::kAuthentication:
            return 401;
        case ErrorType::kAuthorization:
            return 403;
        case ErrorType::kNotFound:
            return 404;
        case ErrorType::kConflict:
            return 409;
        case ErrorType::kDatabase:
        case ErrorType::kConfiguration:
        case ErrorType::kInternal:
            return 500;
    }
    return 500;
}

// Stable, machine-readable code string surfaced to API clients.
[[nodiscard]] constexpr std::string_view code_for(ErrorType type) noexcept {
    switch (type) {
        case ErrorType::kValidation:
            return "validation_error";
        case ErrorType::kAuthentication:
            return "authentication_error";
        case ErrorType::kAuthorization:
            return "authorization_error";
        case ErrorType::kNotFound:
            return "not_found";
        case ErrorType::kConflict:
            return "conflict";
        case ErrorType::kDatabase:
            return "database_error";
        case ErrorType::kConfiguration:
            return "configuration_error";
        case ErrorType::kInternal:
            return "internal_error";
    }
    return "internal_error";
}

}  // namespace rtc::errors
