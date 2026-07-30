#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// Broad category of an uploaded file, derived from its MIME type. Drives
// client rendering and any type-specific processing (e.g. thumbnails for images).
enum class AttachmentKind {
    kImage,
    kPdf,
    kDocument,
    kVideo,
    kAudio,
    kOther,
};

[[nodiscard]] constexpr std::string_view to_string(AttachmentKind kind) noexcept {
    switch (kind) {
        case AttachmentKind::kImage:
            return "image";
        case AttachmentKind::kPdf:
            return "pdf";
        case AttachmentKind::kDocument:
            return "document";
        case AttachmentKind::kVideo:
            return "video";
        case AttachmentKind::kAudio:
            return "audio";
        case AttachmentKind::kOther:
            return "other";
    }
    return "other";
}

[[nodiscard]] constexpr std::optional<AttachmentKind> attachment_kind_from_string(
    std::string_view value) noexcept {
    if (value == "image")
        return AttachmentKind::kImage;
    if (value == "pdf")
        return AttachmentKind::kPdf;
    if (value == "document")
        return AttachmentKind::kDocument;
    if (value == "video")
        return AttachmentKind::kVideo;
    if (value == "audio")
        return AttachmentKind::kAudio;
    if (value == "other")
        return AttachmentKind::kOther;
    return std::nullopt;
}

// Persistent attachment metadata (row in `attachments`). The bytes live in the
// storage backend under `storage_key`.
struct Attachment {
    std::int64_t id = 0;
    std::int64_t owner_id = 0;
    std::optional<std::int64_t> message_id;
    std::string storage_backend = "local";
    std::string storage_key;
    std::optional<std::string> thumbnail_key;
    std::string original_filename;
    std::string content_type;
    AttachmentKind kind = AttachmentKind::kOther;
    std::int64_t byte_size = 0;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::string> checksum;
    utils::TimePoint created_at{};
};

}  // namespace rtc::models
