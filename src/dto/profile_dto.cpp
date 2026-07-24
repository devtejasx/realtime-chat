#include "rtc/dto/profile_dto.hpp"

#include <string>

#include "rtc/errors/exceptions.hpp"

namespace rtc::dto {
namespace {

using rtc::errors::ValidationException;

// Reads an optional-string field with partial-update semantics: if the key is
// absent, `set` stays false; if present and null, `set` is true with a nullopt
// value (clear); if present and a string, `set` is true with that value. Any
// other JSON type is a validation error.
void read_field(const nlohmann::json& body, const char* field, bool& set,
                std::optional<std::string>& value) {
    const auto it = body.find(field);
    if (it == body.end()) {
        return;
    }
    set = true;
    if (it->is_null()) {
        value = std::nullopt;
    } else if (it->is_string()) {
        value = it->get<std::string>();
    } else {
        throw ValidationException(std::string("Field must be a string or null: ") + field,
                                  std::string("field=") + field);
    }
}

}  // namespace

UpdateProfileRequest UpdateProfileRequest::from_json(const nlohmann::json& body) {
    if (!body.is_object()) {
        throw ValidationException("Request body must be a JSON object");
    }
    UpdateProfileRequest request;
    read_field(body, "display_name", request.display_name_set, request.display_name);
    read_field(body, "bio", request.bio_set, request.bio);
    read_field(body, "avatar_url", request.avatar_url_set, request.avatar_url);
    return request;
}

}  // namespace rtc::dto
