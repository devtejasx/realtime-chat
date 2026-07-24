#pragma once

#include <string>
#include <string_view>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/dto/profile_dto.hpp"

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

// Profile field limits (Phase 2). Mirror the users table column widths.
inline constexpr std::size_t kDisplayNameMaxLen = 64;
inline constexpr std::size_t kBioMaxLen = 500;
inline constexpr std::size_t kAvatarUrlMaxLen = 2048;

// Messaging limits (Phase 2).
inline constexpr std::size_t kGroupNameMinLen = 1;
inline constexpr std::size_t kGroupNameMaxLen = 128;
inline constexpr std::size_t kMessageMaxLen = 4000;

// Removes leading/trailing ASCII whitespace.
[[nodiscard]] std::string trim(std::string_view value);

// Individual validators. Each throws rtc::errors::ValidationException with a
// client-safe message and a `field=...` detail on failure.
void validate_username(std::string_view username);
void validate_email(std::string_view email);
void validate_password(std::string_view password);

// Validates a group name (1..128 chars after trimming). Returns the trimmed
// value. Throws ValidationException on failure.
[[nodiscard]] std::string validate_group_name(std::string_view name);

// Validates message content (non-empty after trimming, <= 4000 chars). Returns
// the trimmed content. Throws ValidationException on failure.
[[nodiscard]] std::string validate_message_content(std::string_view content);

// Normalises (trims username/email, lower-cases email) and fully validates a
// register request in place. Throws ValidationException on the first problem.
void validate_and_normalize(dto::RegisterRequest& request);

// Normalises (trims identifier) and validates a login request in place.
void validate_and_normalize(dto::LoginRequest& request);

// Normalises (trims set string values) and validates a profile-update request
// in place. Only fields that were supplied are checked. Throws
// ValidationException on the first invalid field.
void validate_and_normalize(dto::UpdateProfileRequest& request);

}  // namespace rtc::validation
