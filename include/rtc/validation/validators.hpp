#pragma once

#include <string>
#include <string_view>

#include "rtc/dto/auth_dto.hpp"

namespace rtc::validation {

// Field-level constraints, exposed as constants so tests and docs can assert
// against the same source of truth.
inline constexpr std::size_t kUsernameMinLen = 3;
inline constexpr std::size_t kUsernameMaxLen = 32;
inline constexpr std::size_t kEmailMaxLen = 254;
inline constexpr std::size_t kPasswordMinLen = 8;
// bcrypt only considers the first 72 bytes of a password; reject longer inputs
// outright rather than silently truncating (which would be a security bug).
inline constexpr std::size_t kPasswordMaxLen = 72;

// Removes leading/trailing ASCII whitespace.
[[nodiscard]] std::string trim(std::string_view value);

// Individual validators. Each throws rtc::errors::ValidationException with a
// client-safe message and a `field=...` detail on failure.
void validate_username(std::string_view username);
void validate_email(std::string_view email);
void validate_password(std::string_view password);

// Normalises (trims username/email, lower-cases email) and fully validates a
// register request in place. Throws ValidationException on the first problem.
void validate_and_normalize(dto::RegisterRequest& request);

// Normalises (trims identifier) and validates a login request in place.
void validate_and_normalize(dto::LoginRequest& request);

}  // namespace rtc::validation
