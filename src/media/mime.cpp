#include "rtc/media/mime.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rtc::media {
namespace {

using models::AttachmentKind;

// Explicit allow-list → kind. Anything not present is rejected.
const std::unordered_map<std::string, AttachmentKind>& type_map() {
    static const std::unordered_map<std::string, AttachmentKind> kMap = {
        {"image/jpeg", AttachmentKind::kImage},
        {"image/png", AttachmentKind::kImage},
        {"image/gif", AttachmentKind::kImage},
        {"image/webp", AttachmentKind::kImage},
        {"application/pdf", AttachmentKind::kPdf},
        {"text/plain", AttachmentKind::kDocument},
        {"text/csv", AttachmentKind::kDocument},
        {"application/msword", AttachmentKind::kDocument},
        {"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
         AttachmentKind::kDocument},
        {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
         AttachmentKind::kDocument},
        {"video/mp4", AttachmentKind::kVideo},
        {"video/webm", AttachmentKind::kVideo},
        {"video/quicktime", AttachmentKind::kVideo},
        {"audio/mpeg", AttachmentKind::kAudio},
        {"audio/ogg", AttachmentKind::kAudio},
        {"audio/wav", AttachmentKind::kAudio},
        {"audio/webm", AttachmentKind::kAudio},
    };
    return kMap;
}

[[nodiscard]] std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] std::string extension_of(std::string_view filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos) {
        return {};
    }
    return lower(filename.substr(dot + 1));
}

}  // namespace

bool is_allowed_upload_type(std::string_view content_type) {
    return type_map().count(lower(content_type)) > 0;
}

AttachmentKind classify(std::string_view content_type) {
    const auto it = type_map().find(lower(content_type));
    return it == type_map().end() ? AttachmentKind::kOther : it->second;
}

std::string content_type_for_extension(std::string_view filename) {
    static const std::unordered_map<std::string, std::string> kByExt = {
        {"jpg", "image/jpeg"},   {"jpeg", "image/jpeg"},
        {"png", "image/png"},    {"gif", "image/gif"},
        {"webp", "image/webp"},  {"pdf", "application/pdf"},
        {"txt", "text/plain"},   {"csv", "text/csv"},
        {"mp4", "video/mp4"},    {"webm", "video/webm"},
        {"mov", "video/quicktime"}, {"mp3", "audio/mpeg"},
        {"ogg", "audio/ogg"},    {"wav", "audio/wav"},
    };
    const auto it = kByExt.find(extension_of(filename));
    return it == kByExt.end() ? std::string{} : it->second;
}

}  // namespace rtc::media
