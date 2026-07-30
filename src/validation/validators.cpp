#include "rtc/validation/validators.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

#include "rtc/errors/exceptions.hpp"

namespace rtc::validation {
namespace {

using rtc::errors::ValidationException;

// Pragmatic email pattern: one or more non-space/non-@ chars, an @, a domain
// label, a dot, and a TLD. Intentionally not RFC 5322-complete — that is
// counter-productive; final delivery is the real validity test.
const std::regex kEmailPattern(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)",
                               std::regex::ECMAScript | std::regex::icase);

[[nodiscard]] bool is_valid_username_char(char c) {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_' || c == '.' || c == '-';
}

[[nodiscard]] std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

std::string trim(std::string_view value) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    auto begin = std::find_if(value.begin(), value.end(), not_space);
    auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

void validate_username(std::string_view username) {
    if (username.size() < kUsernameMinLen || username.size() > kUsernameMaxLen) {
        throw ValidationException("Username must be between 3 and 32 characters", "field=username");
    }
    const auto uc = static_cast<unsigned char>(username.front());
    if (std::isalnum(uc) == 0) {
        throw ValidationException("Username must start with a letter or digit", "field=username");
    }
    if (!std::all_of(username.begin(), username.end(), is_valid_username_char)) {
        throw ValidationException("Username may only contain letters, digits, '.', '_' and '-'",
                                  "field=username");
    }
}

void validate_email(std::string_view email) {
    if (email.empty() || email.size() > kEmailMaxLen) {
        throw ValidationException("Email must be between 1 and 254 characters", "field=email");
    }
    if (!std::regex_match(email.begin(), email.end(), kEmailPattern)) {
        throw ValidationException("Email format is invalid", "field=email");
    }
}

void validate_password(std::string_view password) {
    if (password.size() < kPasswordMinLen) {
        throw ValidationException("Password must be at least 8 characters", "field=password");
    }
    if (password.size() > kPasswordMaxLen) {
        throw ValidationException("Password must be at most 72 bytes", "field=password");
    }
}

void validate_and_normalize(dto::RegisterRequest& request) {
    request.username = trim(request.username);
    request.email = to_lower(trim(request.email));
    // Password is intentionally not trimmed: surrounding whitespace is
    // significant and stripping it would weaken user-chosen secrets.
    validate_username(request.username);
    validate_email(request.email);
    validate_password(request.password);
}

void validate_and_normalize(dto::LoginRequest& request) {
    request.identifier = trim(request.identifier);
    if (request.identifier.empty()) {
        throw ValidationException("Login identifier is required", "field=identifier");
    }
    if (request.password.empty()) {
        throw ValidationException("Password is required", "field=password");
    }
}

namespace {

// Trims a supplied optional string in place; an all-whitespace value becomes an
// explicit clear (nullopt) so users can't set a blank-but-present profile field.
void normalize_optional(std::optional<std::string>& value) {
    if (value) {
        auto trimmed = trim(*value);
        if (trimmed.empty()) {
            value = std::nullopt;
        } else {
            value = std::move(trimmed);
        }
    }
}

[[nodiscard]] bool is_http_url(std::string_view url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

}  // namespace

std::string validate_group_name(std::string_view name) {
    std::string trimmed = trim(name);
    if (trimmed.size() < kGroupNameMinLen || trimmed.size() > kGroupNameMaxLen) {
        throw ValidationException("Group name must be between 1 and 128 characters", "field=name");
    }
    return trimmed;
}

std::string validate_message_content(std::string_view content) {
    std::string trimmed = trim(content);
    if (trimmed.empty()) {
        throw ValidationException("Message content must not be empty", "field=content");
    }
    if (trimmed.size() > kMessageMaxLen) {
        throw ValidationException("Message content is too long (max 4000 characters)",
                                  "field=content");
    }
    return trimmed;
}

void validate_and_normalize(dto::UpdateProfileRequest& request) {
    if (request.display_name_set) {
        normalize_optional(request.display_name);
        if (request.display_name && request.display_name->size() > kDisplayNameMaxLen) {
            throw ValidationException("Display name must be at most 64 characters",
                                      "field=display_name");
        }
    }
    if (request.bio_set) {
        normalize_optional(request.bio);
        if (request.bio && request.bio->size() > kBioMaxLen) {
            throw ValidationException("Bio must be at most 500 characters", "field=bio");
        }
    }
    if (request.avatar_url_set) {
        normalize_optional(request.avatar_url);
        if (request.avatar_url) {
            if (request.avatar_url->size() > kAvatarUrlMaxLen) {
                throw ValidationException("Avatar URL is too long", "field=avatar_url");
            }
            if (!is_http_url(*request.avatar_url)) {
                throw ValidationException("Avatar URL must be an http(s) URL", "field=avatar_url");
            }
        }
    }
}

}  // namespace rtc::validation
