#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace rtc::dto {

// Inbound payload for PUT /api/users/me.
//
// PUT here has partial-update semantics: a field is only touched when its key
// is present in the request. For each field we therefore track both whether it
// was supplied (`*_set`) and its value (std::optional, where nullopt means an
// explicit JSON null → clear the field). This lets a client update `bio` alone
// without disturbing `display_name`, or clear `avatar_url` by sending null.
struct UpdateProfileRequest {
    bool display_name_set = false;
    std::optional<std::string> display_name;

    bool bio_set = false;
    std::optional<std::string> bio;

    bool avatar_url_set = false;
    std::optional<std::string> avatar_url;

    // Parses the JSON body. Throws rtc::errors::ValidationException on a
    // non-object body or a field of the wrong type.
    [[nodiscard]] static UpdateProfileRequest from_json(const nlohmann::json& body);

    // True when the request would change nothing.
    [[nodiscard]] bool empty() const noexcept {
        return !display_name_set && !bio_set && !avatar_url_set;
    }
};

}  // namespace rtc::dto
