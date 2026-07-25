#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rtc/dto/user_dto.hpp"  // opt_to_json
#include "rtc/models/attachment.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::dto {

// Public representation of an attachment. The `url` (and `thumbnail_url`) are
// download endpoints built from the configured public base URL; raw storage
// keys are never exposed to clients.
struct AttachmentResponse {
    std::int64_t id = 0;
    std::optional<std::int64_t> message_id;
    std::string filename;
    std::string content_type;
    std::string kind;
    std::int64_t byte_size = 0;
    std::optional<int> width;
    std::optional<int> height;
    std::string url;
    std::optional<std::string> thumbnail_url;
    std::string created_at;

    [[nodiscard]] static AttachmentResponse from(const models::Attachment& a,
                                                 std::string_view base_url) {
        AttachmentResponse r;
        r.id = a.id;
        r.message_id = a.message_id;
        r.filename = a.original_filename;
        r.content_type = a.content_type;
        r.kind = std::string(models::to_string(a.kind));
        r.byte_size = a.byte_size;
        r.width = a.width;
        r.height = a.height;
        r.url = std::string(base_url) + "/" + std::to_string(a.id);
        if (a.thumbnail_key) {
            r.thumbnail_url = std::string(base_url) + "/" + std::to_string(a.id) + "/thumbnail";
        }
        r.created_at = utils::to_iso8601(a.created_at);
        return r;
    }

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{
            {"id", id},
            {"message_id", message_id ? nlohmann::json(*message_id) : nlohmann::json(nullptr)},
            {"filename", filename},
            {"content_type", content_type},
            {"kind", kind},
            {"byte_size", byte_size},
            {"width", width ? nlohmann::json(*width) : nlohmann::json(nullptr)},
            {"height", height ? nlohmann::json(*height) : nlohmann::json(nullptr)},
            {"url", url},
            {"thumbnail_url", opt_to_json(thumbnail_url)},
            {"created_at", created_at},
        };
    }
};

}  // namespace rtc::dto
