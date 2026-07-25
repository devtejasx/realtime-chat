#pragma once

#include <string>
#include <string_view>

#include "rtc/models/attachment.hpp"

namespace rtc::media {

// MIME allow-list and classification for uploads.
//
// Security posture (OWASP): only an explicit allow-list of content types is
// accepted; everything else is rejected with 415. The declared content type is
// also cross-checked against the filename extension by the attachment service
// to resist spoofing. Classification maps an accepted type to an AttachmentKind.

// Returns true if the content type is on the upload allow-list.
[[nodiscard]] bool is_allowed_upload_type(std::string_view content_type);

// Maps an (allowed) content type to its AttachmentKind.
[[nodiscard]] models::AttachmentKind classify(std::string_view content_type);

// Best-effort content type inferred from a filename extension ("" if unknown).
[[nodiscard]] std::string content_type_for_extension(std::string_view filename);

}  // namespace rtc::media
